/*
 * This file is part of Anillo OS
 * Copyright (C) 2021 Anillo OS Developers
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <libsys/format.private.h>
#include <stdarg.h>
#include <libsimple/libsimple.h>
#include <gen/libsyscall/syscall-wrappers.h>
#include <libsys/general.h>

// this will NOT retry if only part of the buffer was written
static ferr_t sys_format_write_wrapper(sys_format_write_f write, void* context, const void* buffer, size_t buffer_length, size_t* out_written_count) {
	size_t written_count = 0;
	ferr_t status = write(context, buffer, buffer_length, &written_count);

	if (status == ferr_ok && written_count == 0) {
		status = ferr_temporary_outage;
	}

	*out_written_count += written_count;
	return status;
};

static uint32_t utf8_to_utf32(const char* char_seq, size_t available_bytes, size_t* utf8_length) {
	if (available_bytes == 0) {
		if (utf8_length)
			*utf8_length = 0;
		return UINT32_MAX;
	}

	uint32_t utf32_char = UINT32_MAX;
	uint8_t required_length = 0;

	if (available_bytes > 0) {
		uint8_t first_char = char_seq[0];

		if (first_char & 0x80) {
			if ((first_char & 0x20) == 0) {
				// 2 bytes
				required_length = 2;
				if (available_bytes < required_length)
					goto out;
				utf32_char = ((first_char & 0x1f) << 6) | (char_seq[1] & 0x3f);
			} else if ((first_char & 0x10) == 0) {
				// 3 bytes
				required_length = 3;
				if (available_bytes < required_length)
					goto out;
				utf32_char = ((first_char & 0x0f) << 12) | ((char_seq[1] & 0x3f) << 6) | (char_seq[2] & 0x3f);
			} else if ((first_char & 0x08) == 0) {
				// 4 bytes
				required_length = 4;
				if (available_bytes < required_length)
					goto out;
				utf32_char = ((first_char & 0x07) << 18) | ((char_seq[1] & 0x3f) << 12) | ((char_seq[2] & 0x3f) << 6) | (char_seq[3] & 0x3f);
			} else {
				// more than 4 bytes???
				goto out;
			}
		} else {
			required_length = 1;
			utf32_char = first_char;
		}
	}

out:
	if (utf8_length)
		*utf8_length = required_length;
	return utf32_char;
};

static uint8_t utf32_to_utf8(uint32_t code_point, char* out_bytes) {
	if (code_point < 0x80) {
		out_bytes[0] = code_point;
		return 1;
	} else if (code_point < 0x800) {
		out_bytes[0] = 0xc0 | ((code_point & (0x1fULL << 6)) >> 6);
		out_bytes[1] = 0x80 | (code_point & 0x3f);
		return 2;
	} else if (code_point < 0x10000) {
		out_bytes[0] = 0xe0 | ((code_point & (0x0fULL << 12)) >> 12);
		out_bytes[1] = 0x80 | (code_point & (0x3fULL << 6) >> 6);
		out_bytes[2] = 0x80 | (code_point & 0x3f);
		return 3;
	} else {
		out_bytes[0] = 0xf0 | ((code_point & (0x07ULL << 18)) >> 18);
		out_bytes[1] = 0x80 | (code_point & (0x3fULL << 12) >> 12);
		out_bytes[2] = 0x80 | (code_point & (0x3fULL << 6) >> 6);
		out_bytes[3] = 0x80 | (code_point & 0x3f);
		return 4;
	}
};

static bool read_code_point(const char** string_pointer, size_t* size_pointer, uint32_t* out_code_point) {
	size_t utf8_length = 0;
	uint32_t code_point;
	bool ok;

	if (*size_pointer == 0) {
		ok = false;
		goto out;
	}

	code_point = utf8_to_utf32(*string_pointer, *size_pointer, &utf8_length);
	ok = code_point != UINT32_MAX;

	if (!ok) {
		goto out;
	}

	*string_pointer += utf8_length;
	*size_pointer -= utf8_length;

out:
	if (ok && out_code_point) {
		*out_code_point = code_point;
	}
	return ok;
};

LIBSYS_STRUCT(sys_format_write_context) {
	void* context;
	sys_format_write_f write;
	size_t written_count;
	size_t buffer_length;
	char buffer[128];
};

#define TEMPORARY_OUTAGE_RETRY_COUNT 5

// this will try to write the entire buffer and keep retrying on failures until a certain limit (TEMPORARY_OUTAGE_RETRY_COUNT) is reached,
// in which case it will then report a temporary outage
LIBSYS_WUR static ferr_t flush_buffer(sys_format_write_context_t* context) {
	ferr_t status = ferr_ok;
	uint8_t retry_count = 0;
	size_t written_count = 0;

	while (written_count < context->buffer_length) {
		status = sys_format_write_wrapper(context->write, context->context, &context->buffer[written_count], context->buffer_length - written_count, &written_count);

		if (status == ferr_temporary_outage) {
			if (retry_count >= TEMPORARY_OUTAGE_RETRY_COUNT) {
				goto out;
			}
			++retry_count;
			continue;
		} else if (status != ferr_ok) {
			goto out;
		}
	}

	context->buffer_length = 0;

out:
	context->written_count += written_count;
	return status;
};

LIBSYS_WUR static ferr_t write_buffer(sys_format_write_context_t* context, const char* buffer, size_t buffer_length) {
	ferr_t status = ferr_ok;

	while (buffer_length > 0) {
		size_t writable = sizeof(context->buffer) - context->buffer_length;

		if (writable > buffer_length) {
			writable = buffer_length;
		}

		simple_memcpy(&context->buffer[context->buffer_length], buffer, writable);
		context->buffer_length += writable;
		buffer_length -= writable;
		buffer += writable;

		if (context->buffer_length == sizeof(context->buffer)) {
			status = flush_buffer(context);
			if (status != ferr_ok) {
				goto out;
			}
		}
	}

out:
	return status;
};

LIBSYS_WUR static ferr_t write_code_point(sys_format_write_context_t* context, uint32_t code_point) {
	char utf8[4];
	uint8_t bytes = utf32_to_utf8(code_point, utf8);
	return write_buffer(context, utf8, bytes);
};

static ferr_t format_out_padding(sys_format_write_context_t* context, size_t actual_width, size_t expected_width, bool zero_pad) {
	if (zero_pad) {
		for (size_t i = actual_width; i < expected_width; ++i) {
			ferr_t status = write_code_point(context, '0');
			if (status != ferr_ok) {
				return status;
			}
		}
	} else {
		for (size_t i = actual_width; i < expected_width; ++i) {
			ferr_t status = write_code_point(context, ' ');
			if (status != ferr_ok) {
				return status;
			}
		}
	}
	return ferr_ok;
};

// 32 characters is enough for all three of these variations

LIBSYS_WUR static ferr_t format_out_hex(sys_format_write_context_t* context, uintmax_t value, bool uppercase, size_t width, bool zero_pad) {
	size_t index = 0;
	char buffer[32] = {0};
	ferr_t status = ferr_ok;

	if (width == SIZE_MAX) {
		width = 0;
	}

	if (value == 0) {
		status = format_out_padding(context, 1, width, zero_pad);
		if (status != ferr_ok) {
			goto out;
		}
		status = write_code_point(context, '0');
		goto out;
	}

	while (value > 0) {
		char digit = (char)(value % 16);
		if (digit < 10) {
			buffer[index++] = digit + '0';
		} else {
			buffer[index++] = (digit - 10) + (uppercase ? 'A' : 'a');
		}
		value /= 16;
	}

	status = format_out_padding(context, index, width, zero_pad);
	if (status != ferr_ok) {
		goto out;
	}

	// the buffer is in reverse order, so write it in reverse (to get it forwards)
	for (size_t i = index; i > 0; --i) {
		status = write_code_point(context, buffer[i - 1]);
		if (status != ferr_ok) {
			goto out;
		}
	}

out:
	return status;
};

LIBSYS_WUR static ferr_t format_out_octal(sys_format_write_context_t* context, uintmax_t value, size_t width, bool zero_pad) {
	size_t index = 0;
	char buffer[32] = {0};
	ferr_t status = ferr_ok;

	if (width == SIZE_MAX) {
		width = 0;
	}

	if (value == 0) {
		status = format_out_padding(context, 1, width, zero_pad);
		if (status != ferr_ok) {
			goto out;
		}
		status = write_code_point(context, '0');
		goto out;
	}

	while (value > 0) {
		buffer[index++] = (char)(value % 8) + '0';
		value /= 8;
	}

	status = format_out_padding(context, index, width, zero_pad);
	if (status != ferr_ok) {
		goto out;
	}

	// the buffer is in reverse order, so print it in reverse (to get it forwards)
	for (size_t i = index; i > 0; --i) {
		status = write_code_point(context, buffer[i - 1]);
		if (status != ferr_ok) {
			goto out;
		}
	}

out:
	return status;
};

LIBSYS_WUR static ferr_t format_out_decimal(sys_format_write_context_t* context, uintmax_t value, size_t width, bool zero_pad) {
	size_t index = 0;
	char buffer[32] = {0};
	ferr_t status = ferr_ok;

	if (width == SIZE_MAX) {
		width = 0;
	}

	if (value == 0) {
		status = format_out_padding(context, 1, width, zero_pad);
		if (status != ferr_ok) {
			goto out;
		}
		status = write_code_point(context, '0');
		goto out;
	}

	while (value > 0) {
		buffer[index++] = (char)(value % 10) + '0';
		value /= 10;
	}

	status = format_out_padding(context, index, width, zero_pad);
	if (status != ferr_ok) {
		goto out;
	}

	// the buffer is in reverse order, so print it in reverse (to get it forwards)
	for (size_t i = index; i > 0; --i) {
		status = write_code_point(context, buffer[i - 1]);
		if (status != ferr_ok) {
			goto out;
		}
	}

out:
	return status;
};

LIBSYS_WUR static ferr_t format_out_string(sys_format_write_context_t* context, const char* string, size_t length) {
	return write_buffer(context, string, length);
};

LIBSYS_WUR static ferr_t format_out_floating(sys_format_write_context_t* context, double value) {
	int64_t truncated = (int64_t)value;
	ferr_t status = ferr_ok;
	uint64_t truncated_positive;
	uint64_t remaining;

	if (truncated < 0) {
		status = write_code_point(context, '-');
		if (status != ferr_ok) {
			goto out;
		}
		truncated_positive = truncated * -1;
	} else {
		truncated_positive = truncated;
	}

	status = format_out_decimal(context, truncated_positive, SIZE_MAX, false);
	if (status != ferr_ok) {
		goto out;
	}

	status = write_code_point(context, '.');
	if (status != ferr_ok) {
		goto out;
	}

	remaining = (uint64_t)((value - truncated) * 1e16);
	status = format_out_decimal(context, remaining, SIZE_MAX, false);
	if (status != ferr_ok) {
		goto out;
	}

out:
	return status;
};

ferr_t __sys_format_out(void* context, sys_format_write_f write, size_t* out_written_count, const char* format, size_t format_length, va_list args) {
	ferr_t status = ferr_ok;
	sys_format_write_context_t write_context = {
		.context = context,
		.write = write,
		.written_count = 0,
		.buffer_length = 0,
	};

	if (!format) {
		status = ferr_invalid_argument;
		goto out;
	}

	while (format_length > 0) {
		uint32_t code_point;

		#define READ_NEXT \
			if (!read_code_point(&format, &format_length, &code_point)) { \
				status = ferr_invalid_argument; \
				goto out; \
			} \

		#define WRITE_CODE_POINT(x) \
			status = write_code_point(&write_context, (x)); \
			if (status != ferr_ok) { \
				goto out; \
			}

		READ_NEXT;

		if (code_point == '%') {
			READ_NEXT;

			if (code_point == '%') {
				WRITE_CODE_POINT('%');
				continue;
			}

			LIBSYS_ENUM(uint8_t, format_length) {
				format_length_default,
				format_length_short_short,
				format_length_short,
				format_length_long,
				format_length_long_long,
				format_length_intmax,
				format_length_size,
				format_length_ptrdiff,
			};

			format_length_t length = format_length_default;
			size_t precision = SIZE_MAX;
			size_t width = SIZE_MAX;
			bool zero_pad = false;

			if (code_point == '0') {
				READ_NEXT;

				zero_pad = true;
			}

			if (code_point >= '0' && code_point <= '9') {
				--format;
				++format_length;

				const char* num_start = format;
				const char* num_end = num_start;

				while (*num_end >= '0' && *num_end <= '9') {
					++num_end;
					++format;
					--format_length;
				}

				READ_NEXT;

				if (num_end != num_start) {
					if (simple_string_to_integer_unsigned(num_start, num_end - num_start, NULL, 10, &width) != ferr_ok) {
						status = ferr_invalid_argument;
						goto out;
					}
				}
			}

			if (code_point == '.') {
				READ_NEXT;

				if (code_point == '*') {
					READ_NEXT;

					precision = va_arg(args, int);
				} else {
					const char* num_start = format;
					const char* num_end = num_start;

					while (*num_end >= '0' && *num_end <= '9') {
						++num_end;
						++format;
						--format_length;
					}

					READ_NEXT;

					if (num_end != num_start) {
						if (simple_string_to_integer_unsigned(num_start, num_end - num_start, NULL, 10, &precision) != ferr_ok) {
							status = ferr_invalid_argument;
							goto out;
						}
					}
				}
			}

			if (code_point == 'h') {
				READ_NEXT;

				if (code_point == 'h') {
					READ_NEXT;

					length = format_length_short_short;
				} else {
					length = format_length_short;
				}
			} else if (code_point == 'l') {
				READ_NEXT;

				if (code_point == 'l') {
					READ_NEXT;

					length = format_length_long_long;
				} else {
					length = format_length_long;
				}
			} else if (code_point == 'j') {
				READ_NEXT;

				length = format_length_intmax;
			} else if (code_point == 'z') {
				READ_NEXT;

				length = format_length_size;
			} else if (code_point == 't') {
				READ_NEXT;

				length = format_length_ptrdiff;
			}

			switch (code_point) {
				case 'd':
				case 'i': {
					intmax_t value = 0;

					switch (length) {
						case format_length_default:
						case format_length_short_short:
						case format_length_short:
							value = va_arg(args, int);
							break;
						case format_length_long:
							value = va_arg(args, long int);
							break;
						case format_length_long_long:
							value = va_arg(args, long long int);
							break;
						case format_length_intmax:
							value = va_arg(args, intmax_t);
							break;
						case format_length_size:
							value = va_arg(args, size_t);
							break;
						case format_length_ptrdiff:
							value = va_arg(args, ptrdiff_t);
							break;
					}

					if (value < 0) {
						WRITE_CODE_POINT('-');
						value *= -1;

						if (width != SIZE_MAX) {
							width = (width < 1) ? 0 : (width - 1);
						}
					}

					status = format_out_decimal(&write_context, value, width, zero_pad);
					if (status != ferr_ok) {
						goto out;
					}
				} break;

				case 'u':
				case 'o':
				case 'x':
				case 'X': {
					uintmax_t value = 0;

					switch (length) {
						case format_length_default:
						case format_length_short_short:
						case format_length_short:
							value = va_arg(args, unsigned int);
							break;
						case format_length_long:
							value = va_arg(args, unsigned long int);
							break;
						case format_length_long_long:
							value = va_arg(args, unsigned long long int);
							break;
						case format_length_intmax:
							value = va_arg(args, uintmax_t);
							break;
						case format_length_size:
							value = va_arg(args, size_t);
							break;
						case format_length_ptrdiff:
							value = va_arg(args, ptrdiff_t);
							break;
					}

					if (code_point == 'x' || code_point == 'X') {
						status = format_out_hex(&write_context, value, code_point == 'X', width, zero_pad);
					} else if (code_point == 'o') {
						status = format_out_octal(&write_context, value, width, zero_pad);
					} else {
						status = format_out_decimal(&write_context, value, width, zero_pad);
					}

					if (status != ferr_ok) {
						goto out;
					}
				} break;

				case 'c': {
					char value = (char)va_arg(args, int);
					WRITE_CODE_POINT(value);
				} break;

				case 's': {
					const char* value = va_arg(args, const char*);
					status = format_out_string(&write_context, value, precision == SIZE_MAX ? simple_strlen(value) : simple_strnlen(value, precision));
					if (status != ferr_ok) {
						goto out;
					}
				} break;

				case 'p': {
					const void* value = va_arg(args, const void*);

					if (width == SIZE_MAX) {
						width = 18;
						zero_pad = true;
					}

					// in reality, this should pad to 16 characters (not including "0x")
					WRITE_CODE_POINT('0');
					WRITE_CODE_POINT('x');
					status = format_out_hex(&write_context, (uintmax_t)(uintptr_t)value, false, width, zero_pad);
					if (status != ferr_ok) {
						goto out;
					}
				} break;

				case 'f':
				case 'F': {
					double value = va_arg(args, double);

					// TODO: support zero-padding for this format

					status = format_out_floating(&write_context, value);
				} break;

				default: {
					// invalid format
					status = ferr_invalid_argument;
					goto out;
				} break;
			}
		} else {
			WRITE_CODE_POINT(code_point);
		}

		#undef READ_NEXT
		#undef WRITE_CODE_POINT
	}

out:
	if (status == ferr_ok) {
		status = flush_buffer(&write_context);
	}
	if (out_written_count) {
		*out_written_count = write_context.written_count;
	}
	return status;
};

sys_format_out_console_hook_f __sys_format_out_console_hook = NULL;

SYS_FORMAT_VARIANT_WRAPPER(console, {}, {}, int ignored) {
	SYS_FORMAT_WRITE_HEADER(console);

#if BUILDING_DYMPLE
	ferr_t status = ferr_ok;
	LIBSYS_WUR_IGNORE(sys_kernel_log_n(buffer, buffer_length));
#else
	ferr_t status = ferr_temporary_outage;
	if (__sys_format_out_console_hook) {
		status = __sys_format_out_console_hook(buffer, buffer_length);
	}
#endif

	if (status == ferr_ok) {
		*out_written_count += buffer_length;
	}

	return status;
};

SYS_FORMAT_VARIANT_WRAPPER(buffer, { char* buffer; size_t buffer_size; }, SYS_FORMAT_CONTEXT_INIT(buffer, buffer_size), void* buffer, size_t buffer_size) {
	SYS_FORMAT_WRITE_HEADER(buffer);
	size_t size_to_write = (buffer_length < context->buffer_size) ? buffer_length : context->buffer_size;

	*out_written_count += buffer_length;

	simple_memcpy(context->buffer, buffer, size_to_write);
	context->buffer += size_to_write;
	context->buffer_size -= size_to_write;

	return ferr_ok;
};
