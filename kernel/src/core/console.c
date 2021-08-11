/**
 * This file is part of Anillo OS
 * Copyright (C) 2020 Anillo OS Developers
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
//
// console.c
//
// console implementation for the kernel that starts working on early boot and continues later on
//

#include <stdint.h>
#include <stdbool.h>

#include <ferro/core/console.h>
#include <ferro/core/framebuffer.h>
#include <ferro/core/mempool.h>
#include <ferro/core/panic.h>
#include <ferro/core/locks.h>
#include <libk/libk.h>

#include <gen/ferro/font.h>

#define PSF_FLAG_UNICODE 1

FERRO_PACKED_STRUCT(ferro_console_font) {
	uint32_t magic;
	uint32_t version;
	uint32_t header_size;
	uint32_t flags;
	uint32_t glyph_count;
	uint32_t glyph_size;
	uint32_t glyph_height;
	uint32_t glyph_width;
	uint8_t glyphs[];
};

ferro_console_font_t* font = (ferro_console_font_t*)font_data;

// protects a single character from being written
// (this is so that we don't get jumbled character writes)
flock_spin_intsafe_t character_log_lock = FLOCK_SPIN_INTSAFE_INIT;

/**
 * Translates a single character from UTF-8 encoding into UTF-32 encoding.
 *
 * @param char_seq        UTF-8 representation of the character to transcode.
 * @param available_bytes Number of bytes available to be read from `char_seq`.
 * @param utf8_length     A pointer to a variable in which the number of bytes the UTF-8 representation occupies will be written. May be `NULL`. If provided, it is always written to with the required number of bytes, regardless of success or failure.
 *
 * @returns UINT32_MAX if an error occurred (i.e. invalid sequence or insufficient bytes), or the UTF-32 representation of the character otherwise.
 */
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

static ferr_t fconsole_put_utf32_char(uint32_t unichar, size_t x, size_t y, const ferro_fb_pixel_t* foreground, const ferro_fb_pixel_t* background) {
	uint16_t index = (font->flags & PSF_FLAG_UNICODE) ? unicode_map[unichar] : (uint16_t)(unichar & 0xffff);
	uint8_t* glyph;

	if (index > font->glyph_count) {
		index = 0;
	}

	glyph = &font->glyphs[index * font->glyph_size];

	for (uint32_t glyph_y = 0; glyph_y < font->glyph_height; ++glyph_y) {
		uint8_t* row = &glyph[glyph_y * ((font->glyph_width + 7) / 8)];

		for (uint32_t glyph_x = 0; glyph_x < font->glyph_width; ++glyph_x) {
			(void)ferro_fb_set_pixel((row[glyph_x / 8] & (1 << (7 - (glyph_x % 8)))) ? foreground : background, x + glyph_x, y + glyph_y);
		}
	}

	return ferr_ok;
};

static ferro_fb_pixel_t white_pixel = {
	.red = 0xff,
	.green = 0xff,
	.blue = 0xff,
};
static ferro_fb_pixel_t black_pixel = {
	.red = 0,
	.green = 0,
	.blue = 0,
};

static ferro_fb_coords_t next_location = {
	.x = 0,
	.y = 0,
};

#ifndef FCONSOLE_CHARACTER_PADDING_DEFAULT
	#define FCONSOLE_CHARACTER_PADDING_DEFAULT 0
#endif
#ifndef FCONSOLE_LINE_PADDING_DEFAULT
	#define FCONSOLE_LINE_PADDING_DEFAULT 0
#endif
static size_t character_padding = FCONSOLE_CHARACTER_PADDING_DEFAULT;
static size_t line_padding = FCONSOLE_LINE_PADDING_DEFAULT;

void fconsole_init() {
	fconsole_log("ferro kernel version 0.0.0 starting...\n");

#if 0
	const char* orig = "foo!\n";
	char* copied = NULL;

	if (fmempool_allocate(strlen(orig) + 1, NULL, (void*)&copied) != ferr_ok) {
		fpanic(NULL);
	}

	memcpy(copied, orig, strlen(orig) + 1);

	fconsole_log(copied);

	if (fmempool_free(copied) != ferr_ok) {
		fpanic(NULL);
	}
#endif
};

static void fconsole_log_code_point(uint32_t code_point) {
	const ferro_fb_info_t* fb_info = ferro_fb_get_info();
	bool print_it = true;

	if (!fb_info) {
		return;
	}

	if (code_point == '\n') {
		print_it = false;
	}

	flock_spin_intsafe_lock(&character_log_lock);

	if (code_point == '\n' || next_location.x + font->glyph_width >= fb_info->width) {
		next_location.x = 0;
		next_location.y += font->glyph_height + line_padding;
	}
	if (next_location.y + font->glyph_height >= fb_info->height) {
		(void)ferro_fb_shift(true, font->glyph_height + line_padding, &black_pixel);
		next_location.y -= font->glyph_height + line_padding;
	}

	if (print_it) {
		fconsole_put_utf32_char(code_point, next_location.x, next_location.y, &white_pixel, &black_pixel);
		next_location.x += font->glyph_width + character_padding;
	}

	flock_spin_intsafe_unlock(&character_log_lock);
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

ferr_t fconsole_logn(const char* string, size_t size) {
	while (size > 0) {
		uint32_t code_point;

		if (!read_code_point(&string, &size, &code_point)) {
			goto err_out;
		}

		fconsole_log_code_point(code_point);
	}

	return ferr_ok;
err_out:
	return ferr_invalid_argument;
};

ferr_t fconsole_log(const char* string) {
	return fconsole_logn(string, strlen(string));
};

// 32 characters is enough for all three of these variations

static void print_hex(uintmax_t value, bool uppercase) {
	size_t index = 0;
	char buffer[32] = {0};

	if (value == 0) {
		fconsole_log_code_point('0');
		return;
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

	// the buffer is in reverse order, so print it in reverse (to get it forwards)
	for (size_t i = index; i > 0; --i) {
		fconsole_log_code_point(buffer[i - 1]);
	}
};

static void print_octal(uintmax_t value) {
	size_t index = 0;
	char buffer[32] = {0};

	if (value == 0) {
		fconsole_log_code_point('0');
		return;
	}

	while (value > 0) {
		buffer[index++] = (char)(value % 8) + '0';
		value /= 8;
	}

	// the buffer is in reverse order, so print it in reverse (to get it forwards)
	for (size_t i = index; i > 0; --i) {
		fconsole_log_code_point(buffer[i - 1]);
	}
};

static void print_decimal(uintmax_t value) {
	size_t index = 0;
	char buffer[32] = {0};

	if (value == 0) {
		fconsole_log_code_point('0');
		return;
	}

	while (value > 0) {
		buffer[index++] = (char)(value % 10) + '0';
		value /= 10;
	}

	// the buffer is in reverse order, so print it in reverse (to get it forwards)
	for (size_t i = index; i > 0; --i) {
		fconsole_log_code_point(buffer[i - 1]);
	}
};

ferr_t fconsole_lognfv(const char* format, size_t format_size, va_list args) {
	while (format_size > 0) {
		uint32_t code_point;

		#define READ_NEXT \
			if (!read_code_point(&format, &format_size, &code_point)) { \
				goto err_out; \
			} \

		READ_NEXT;

		if (code_point == '%') {
			READ_NEXT;

			if (code_point == '%') {
				fconsole_log_code_point('%');
				continue;
			}

			FERRO_ENUM(uint8_t, printf_length) {
				printf_length_default,
				printf_length_short_short,
				printf_length_short,
				printf_length_long,
				printf_length_long_long,
				printf_length_intmax,
				printf_length_size,
				printf_length_ptrdiff,
			};

			printf_length_t length = printf_length_default;

			if (code_point == 'h') {
				READ_NEXT;

				if (code_point == 'h') {
					READ_NEXT;

					length = printf_length_short_short;
				} else {
					length = printf_length_short;
				}
			} else if (code_point == 'l') {
				READ_NEXT;

				if (code_point == 'l') {
					READ_NEXT;

					length = printf_length_long_long;
				} else {
					length = printf_length_long;
				}
			} else if (code_point == 'j') {
				READ_NEXT;

				length = printf_length_intmax;
			} else if (code_point == 'z') {
				READ_NEXT;

				length = printf_length_size;
			} else if (code_point == 't') {
				READ_NEXT;

				length = printf_length_ptrdiff;
			}

			switch (code_point) {
				case 'd':
				case 'i': {
					intmax_t value = 0;

					switch (length) {
						case printf_length_default:
						case printf_length_short_short:
						case printf_length_short:
							value = va_arg(args, int);
							break;
						case printf_length_long:
							value = va_arg(args, long int);
							break;
						case printf_length_long_long:
							value = va_arg(args, long long int);
							break;
						case printf_length_intmax:
							value = va_arg(args, intmax_t);
							break;
						case printf_length_size:
							value = va_arg(args, size_t);
							break;
						case printf_length_ptrdiff:
							value = va_arg(args, ptrdiff_t);
							break;
					}

					if (value < 0) {
						fconsole_log_code_point('-');
						value *= -1;
					}

					print_decimal(value);
				} break;

				case 'u':
				case 'o':
				case 'x':
				case 'X': {
					uintmax_t value = 0;

					switch (length) {
						case printf_length_default:
						case printf_length_short_short:
						case printf_length_short:
							value = va_arg(args, unsigned int);
							break;
						case printf_length_long:
							value = va_arg(args, unsigned long int);
							break;
						case printf_length_long_long:
							value = va_arg(args, unsigned long long int);
							break;
						case printf_length_intmax:
							value = va_arg(args, uintmax_t);
							break;
						case printf_length_size:
							value = va_arg(args, size_t);
							break;
						case printf_length_ptrdiff:
							value = va_arg(args, ptrdiff_t);
							break;
					}

					if (code_point == 'x' || code_point == 'X') {
						print_hex(value, code_point == 'X');
					} else if (code_point == 'o') {
						print_octal(value);
					} else {
						print_decimal(value);
					}
				} break;

				case 'c': {
					char value = (char)va_arg(args, int);
					fconsole_log_code_point(value);
				} break;

				case 's': {
					const char* value = va_arg(args, const char*);
					fconsole_log(value);
				} break;

				case 'p': {
					const void* value = va_arg(args, const void*);

					// in reality, this should pad to 16 characters (not including "0x")
					fconsole_log_code_point('0');
					fconsole_log_code_point('x');
					print_hex((uintmax_t)(uintptr_t)value, false);
				} break;

				default: {
					// invalid format
					return -1;
				} break;
			}
		} else {
			fconsole_log_code_point(code_point);
		}

		#undef READ_NEXT
	}

	return ferr_ok;
err_out:
	return ferr_invalid_argument;
};

ferr_t fconsole_lognf(const char* format, size_t format_size, ...) {
	va_list args;
	ferr_t result;

	va_start(args, format_size);
	result = fconsole_lognfv(format, format_size, args);
	va_end(args);

	return result;
};

ferr_t fconsole_logfv(const char* format, va_list args) {
	return fconsole_lognfv(format, strlen(format), args);
};

ferr_t fconsole_logf(const char* format, ...) {
	va_list args;
	ferr_t result;

	va_start(args, format);
	result = fconsole_logfv(format, args);
	va_end(args);

	return result;
};
