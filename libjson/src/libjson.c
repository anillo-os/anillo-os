/*
 * This file is part of Anillo OS
 * Copyright (C) 2023 Anillo OS Developers
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

#include <libjson/libjson.h>

LIBJSON_STRUCT(json_object_stack) {
	json_object_t* object;
	char* pending_key;
	size_t pending_key_length;
};

static uint32_t utf8_to_utf32(const char* sequence, size_t max_length, size_t* out_utf8_length) {
	uint32_t utf32_char = UINT32_MAX;
	uint8_t required_length = 0;

	if (max_length > 0) {
		uint8_t first_char = sequence[0];

		if (first_char & 0x80) {
			if ((first_char & 0x20) == 0) {
				// 2 bytes
				required_length = 2;
				if (max_length < required_length)
					goto out;
				utf32_char = ((first_char & 0x1f) << 6) | (sequence[1] & 0x3f);
			} else if ((first_char & 0x10) == 0) {
				// 3 bytes
				required_length = 3;
				if (max_length < required_length)
					goto out;
				utf32_char = ((first_char & 0x0f) << 12) | ((sequence[1] & 0x3f) << 6) | (sequence[2] & 0x3f);
			} else if ((first_char & 0x08) == 0) {
				// 4 bytes
				required_length = 4;
				if (max_length < required_length)
					goto out;
				utf32_char = ((first_char & 0x07) << 18) | ((sequence[1] & 0x3f) << 12) | ((sequence[2] & 0x3f) << 6) | (sequence[3] & 0x3f);
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
	if (out_utf8_length) {
		*out_utf8_length = required_length;
	}
	return utf32_char;
};

LIBJSON_ALWAYS_INLINE bool is_hex_digit(char character) {
	return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f') || (character >= 'A' && character <= 'F');
};

ferr_t json_parse_string(const char* string, bool json5, json_object_t** out_object) {
	return json_parse_string_n(string, simple_strlen(string), json5, out_object);
};

ferr_t json_parse_file(sys_file_t* file, bool json5, json_object_t** out_object) {
	ferr_t status = ferr_ok;
	sys_file_info_t info;
	sys_data_t* data = NULL;

	status = sys_file_get_info(file, &info);
	if (status != ferr_ok) {
		goto out;
	}

	status = sys_file_read_data(file, 0, info.size, &data);
	if (status != ferr_ok) {
		goto out;
	}

	status = json_parse_string_n(sys_data_contents(data), sys_data_length(data), json5, out_object);

out:
	if (data) {
		sys_release(data);
	}
	return status;
};

ferr_t json_parse_string_object(const char* buffer, size_t buffer_length, bool json5, size_t* out_characters, char** out_string, size_t* out_string_length) {
	ferr_t status = ferr_ok;
	size_t offset = 0;
	size_t parsed_length = 0;
	char* parsed = NULL;
	bool singleQuoteString = false;
	size_t parsed_index = 0;

	if (buffer_length < 2) {
		// a string requires at least the opening and closing quotation marks
		status = ferr_invalid_argument;
		goto out;
	}

	if (json5 && buffer[offset] == '\'') {
		singleQuoteString = true;
	} else if (buffer[offset] != '"') {
		status = ferr_invalid_argument;
		goto out;
	}

	// consume the leading quotation mark
	++offset;

	for (; offset < buffer_length; ++offset) {
		if ((singleQuoteString && buffer[offset] == '\'') || (!singleQuoteString && buffer[offset] == '"')) {
			// consume the terminating quotation mark and terminate the string
			++offset;
			break;
		} else if (buffer[offset] == '\\') {
			// either an escape sequence or (for JSON5) a line continuation

			if (offset + 1 >= buffer_length) {
				// no character following escape?
				status = ferr_invalid_argument;
				goto out;
			}

			// consume the escape
			++offset;

			switch (buffer[offset]) {
				case '"':
				case '\\':
				case '/':
				case 'b':
				case 'f':
				case 'n':
				case 'r':
				case 't':
				case '\'': // technically only valid for JSON5, but since we accept other escaped characters in our default case, just pass it through regardless
				case '0':  // ditto
				case 'v':  // ditto
					// all of these escape sequence produce a single character
					++parsed_length;

					// the character is consumed by the loop's increment
					break;

				case 'u':
					if (offset + 4 >= buffer_length) {
						// not enough characters for Unicode escape sequence
						status = ferr_invalid_argument;
						goto out;
					}

					// consume the 'u'
					++offset;

					if (!is_hex_digit(buffer[offset]) || !is_hex_digit(buffer[offset + 1]) || !is_hex_digit(buffer[offset + 2]) || !is_hex_digit(buffer[offset + 3])) {
						// following characters aren't hex digits
						status = ferr_invalid_argument;
						goto out;
					}

					// consume the first three hex digits (the last hex digit is consumed by the loop's increment)
					offset += 4;

					// a Unicode escape sequence requires 2 bytes to be represented
					parsed_length += 2;
					break;

				case 'x':
					if (json5) {
						if (offset + 2 >= buffer_length) {
							// not enough characters for hex escape sequence
							status = ferr_invalid_argument;
							goto out;
						}

						// consume the 'x'
						++offset;

						if (!is_hex_digit(buffer[offset]) || !is_hex_digit(buffer[offset + 1])) {
							// following characters aren't hex digits
							status = ferr_invalid_argument;
							goto out;
						}

						// consume the first hex digit (the second is consumed by the loop's increment)
						++offset;

						// a hex escape sequence requires 1 byte to be represented
						++parsed_length;
					} else {
						// otherwise, we just pass it through like the default case
						++parsed_length;
					}
					break;

				case '\r':
					if (offset + 1 >= buffer_length && buffer[offset + 1] == '\n') {
						// consume the carriage return
						++offset;
					}
					// fallthrough
				case '\n':
					if (!json5) {
						// line contiuations are only allowed in JSON5
						status = ferr_invalid_argument;
						goto out;
					}
					// the newline is consumed by the loop's increment

					// do *not* increment `parsed_length`: a line continuation eliminates the line terminator characters from the string
					break;

				default: {
					uint32_t u32 = utf8_to_utf32(&buffer[offset], buffer_length - offset, NULL);
					if (buffer[offset] < 0x20) {
						// we don't accept control characters inside strings
						//
						// the only time we accept *certain* control characters is when JSON5 is enabled and the character is a line terminator
						status = ferr_invalid_argument;
						goto out;
					} else if (json5 && u32 == 0x2028 || u32 == 0x2029) {
						// these count as line terminators, so this is a line continuation and `parsed_length` is *not* incremented
					} else {
						// for any other character, this is technically an invalid escape sequence, but let's accept it anyways
						// (it'll just produce the character itself)
						//
						// this character is consumed by the loop's increment
						++parsed_length;
					}
				} break;
			}
		} else if (buffer[offset] < 0x20) {
			// control characters are not allowed
			status = ferr_invalid_argument;
			goto out;
		} else {
			++parsed_length;
		}
	}

	// now that we've checked this is indeed a valid string, let's populate it
	status = sys_mempool_allocate(parsed_length, NULL, (void*)&parsed);
	if (status != ferr_ok) {
		goto out;
	}

	json_assert(offset >= 2);
	for (size_t i = 1; i < offset - 1; ++i) {
		if (buffer[i] == '\\') {
			fassert(i + 1 < buffer_length);

			// consume the escape
			++i;

			switch (buffer[offset]) {
				case '"':
					parsed[parsed_index++] = '"';
					break;
				case '\\':
					parsed[parsed_index++] = '\\';
					break;
				case '/':
					parsed[parsed_index++] = '/';
					break;
				case 'b':
					parsed[parsed_index++] = '\b';
					break;
				case 'f':
					parsed[parsed_index++] = '\f';
					break;
				case 'n':
					parsed[parsed_index++] = '\n';
					break;
				case 'r':
					parsed[parsed_index++] = '\r';
					break;
				case 't':
					parsed[parsed_index++] = '\t';
					break;
				case '\'':
					// technically only valid for JSON5, but since we accept other escaped characters in our default case, just pass it through regardless
					parsed[parsed_index++] = '\'';
					break;
				case '0':
					if (json5) {
						parsed[parsed_index++] = '\0';
					} else {
						// technically, this is an invalid escape, but just pass it through
						parsed[parsed_index++] = '0';
					}
					break;
				case 'v':
					if (json5) {
						parsed[parsed_index++] = '\v';
					} else {
						// technically, this is an invalid escape, but just pass it through
						parsed[parsed_index++] = 'v';
					}
					break;

				case 'u':
					fassert(offset + 4 < buffer_length);

					// consume the 'u'
					++i;

					fassert(is_hex_digit(buffer[offset]) && is_hex_digit(buffer[offset + 1]) && is_hex_digit(buffer[offset + 2]) && is_hex_digit(buffer[offset + 3]));

					// TODO WORKING HERE
					break;

				case 'x':
					if (json5) {
						if (offset + 2 >= buffer_length) {
							// not enough characters for hex escape sequence
							status = ferr_invalid_argument;
							goto out;
						}

						// consume the 'x'
						++offset;

						if (!is_hex_digit(buffer[offset]) || !is_hex_digit(buffer[offset + 1])) {
							// following characters aren't hex digits
							status = ferr_invalid_argument;
							goto out;
						}

						// consume the first hex digit (the second is consumed by the loop's increment)
						++offset;

						// a hex escape sequence requires 1 byte to be represented
						++parsed_length;
					} else {
						// otherwise, we just pass it through like the default case
						++parsed_length;
					}
					break;

				case '\r':
					if (offset + 1 >= buffer_length && buffer[offset + 1] == '\n') {
						// consume the carriage return
						++offset;
					}
					// fallthrough
				case '\n':
					if (!json5) {
						// line contiuations are only allowed in JSON5
						status = ferr_invalid_argument;
						goto out;
					}
					// the newline is consumed by the loop's increment

					// do *not* increment `parsed_length`: a line continuation eliminates the line terminator characters from the string
					break;

				default: {
					uint32_t u32 = utf8_to_utf32(&buffer[offset], buffer_length - offset, NULL);
					if (buffer[offset] < 0x20) {
						// we don't accept control characters inside strings
						//
						// the only time we accept *certain* control characters is when JSON5 is enabled and the character is a line terminator
						status = ferr_invalid_argument;
						goto out;
					} else if (json5 && u32 == 0x2028 || u32 == 0x2029) {
						// these count as line terminators, so this is a line continuation and `parsed_length` is *not* incremented
					} else {
						// for any other character, this is technically an invalid escape sequence, but let's accept it anyways
						// (it'll just produce the character itself)
						//
						// this character is consumed by the loop's increment
						++parsed_length;
					}
				} break;
			}
		} else {
			fassert(buffer[i] >= 0x20);
			parsed[parsed_index++] = buffer[i];
		}
	}

	*out_characters = offset;
	*out_string = parsed;
	*out_string_length = parsed_length;

out:
	return status;
};

ferr_t json_parse_string_n(const char* string, size_t string_length, bool json5, json_object_t** out_object) {
	json_object_stack_t* object_stack = NULL;
	size_t object_stack_size = 0;
	ferr_t status = ferr_ok;
	json_object_t* result = NULL;

	while (string_length > 0) {

	}

	if (object_stack_size > 0) {
		status = ferr_invalid_argument;
		goto out;
	}

	if (!result) {
		status = ferr_invalid_argument;
		goto out;
	}

	*out_object = result;

out:
	if (object_stack_size > 0) {
		for (size_t i = 0; i < object_stack_size; ++i) {
			if (object_stack[i].pending_key) {
				LIBJSON_WUR_IGNORE(sys_mempool_free(object_stack[i].pending_key));
			}
			if (object_stack[i].object) {
				LIBJSON_WUR_IGNORE(json_release(object_stack[i].object));
			}
		}
		LIBJSON_WUR_IGNORE(sys_mempool_free(object_stack));
	}
	return status;
};
