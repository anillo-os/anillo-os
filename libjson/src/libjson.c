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

LIBJSON_ALWAYS_INLINE bool is_hex_digit(char character) {
	return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f') || (character >= 'A' && character <= 'F');
};

LIBJSON_ALWAYS_INLINE uint8_t hex_digit_value(char character) {
	if (character >= 'a' && character <= 'f') {
		return (character - 'a') + 10;
	} else if (character >= 'A' && character <= 'F') {
		return (character - 'A') + 10;
	} else if (character >= '0' && character <= '9') {
		return character - '0';
	} else {
		return UINT8_MAX;
	}
};

LIBJSON_ALWAYS_INLINE bool json_isspace(char character) {
	return character == ' ' || character == '\t' || character == '\n' || character == '\r';
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
	uint16_t* parsed = NULL;
	bool singleQuoteString = false;
	size_t parsed_index = 0;
	char* result = NULL;
	size_t utf8_length = 0;
	size_t utf8_index = 0;

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
					offset += 3;

					// a Unicode escape sequence only requires 1 UTF-16 word to be represented
					++parsed_length;
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

						// a hex escape sequence requires 1 word (actually, just 1 byte) to be represented
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
					uint32_t u32 = UINT32_MAX;
					status = simple_utf8_to_utf32(&buffer[offset], buffer_length - offset, NULL, &u32);
					if (status != ferr_ok) {
						goto out;
					}
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
						size_t utf16_length = 0;
						status = simple_utf8_to_utf16(&buffer[offset], buffer_length - offset, 0, NULL, NULL, &utf16_length);
						if (status != ferr_ok) {
							goto out;
						}
						parsed_length += utf16_length;
					}
				} break;
			}
		} else if (buffer[offset] < 0x20) {
			// control characters are not allowed
			status = ferr_invalid_argument;
			goto out;
		} else {
			size_t utf16_length = 0;
			status = simple_utf8_to_utf16(&buffer[offset], buffer_length - offset, 0, NULL, NULL, &utf16_length);
			if (status != ferr_ok) {
				goto out;
			}
			parsed_length += utf16_length;
		}
	}

	// now that we've checked this is indeed a valid string, let's populate it
	status = sys_mempool_allocate(sizeof(uint16_t) * parsed_length, NULL, (void*)&parsed);
	if (status != ferr_ok) {
		goto out;
	}

	json_assert(offset >= 2);
	for (size_t i = 1; i < offset - 1; ++i) {
		if (buffer[i] == '\\') {
			fassert(i + 1 < buffer_length);

			// consume the escape
			++i;

			switch (buffer[i]) {
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

				case 'u': {
					uint16_t utf16;

					fassert(i + 4 < buffer_length);

					// consume the 'u'
					++i;

					fassert(is_hex_digit(buffer[i]) && is_hex_digit(buffer[i + 1]) && is_hex_digit(buffer[i + 2]) && is_hex_digit(buffer[i + 3]));

					parsed[parsed_index++] =
						((uint16_t)hex_digit_value(buffer[i    ]) << 12) |
						((uint16_t)hex_digit_value(buffer[i + 1]) <<  8) |
						((uint16_t)hex_digit_value(buffer[i + 2]) <<  4) |
						((uint16_t)hex_digit_value(buffer[i + 3]) <<  0) ;

					// consume the first three hex digits (the last hex digit is consumed by the loop's increment)
					i += 3;
				} break;

				case 'x':
					if (json5) {
						fassert(i + 2 >= buffer_length);

						// consume the 'x'
						++i;

						fassert(is_hex_digit(buffer[i]) && is_hex_digit(buffer[i + 1]));

						parsed[parsed_index++] =
							((uint16_t)hex_digit_value(buffer[i    ]) << 4) |
							((uint16_t)hex_digit_value(buffer[i + 1]) << 0) ;

						// consume the first hex digit (the second is consumed by the loop's increment)
						++i;
					} else {
						// otherwise, we just pass it through like the default case
						parsed[parsed_index++] = 'x';
					}
					break;

				case '\r':
					if (i + 1 >= buffer_length && buffer[i + 1] == '\n') {
						// consume the carriage return
						++i;
					}
					// fallthrough
				case '\n':
					fassert(json5);
					// the newline is consumed by the loop's increment

					// a line continuation eliminates the line terminator characters from the string
					break;

				default: {
					uint32_t u32 = UINT32_MAX;
					fassert(simple_utf8_to_utf32(&buffer[offset], buffer_length - offset, NULL, &u32) == ferr_ok);
					fassert(u32 >= 0x20);
					if (json5 && u32 == 0x2028 || u32 == 0x2029) {
						// these count as line terminators, so this is a line continuation these are eliminated from the string
					} else {
						// for any other character, this is technically an invalid escape sequence, but let's accept it anyways
						// (it'll just produce the character itself)
						//
						// this character is consumed by the loop's increment
						size_t utf16_length = 0;
						fassert(simple_utf8_to_utf16(&buffer[i], buffer_length - i, parsed_length - parsed_index, NULL, &parsed[parsed_index], &utf16_length) == ferr_ok);
						parsed_index += utf16_length;
					}
				} break;
			}
		} else {
			fassert(buffer[i] >= 0x20);
			size_t utf16_length = 0;
			fassert(simple_utf8_to_utf16(&buffer[i], buffer_length - i, parsed_length - parsed_index, NULL, &parsed[parsed_index], &utf16_length) == ferr_ok);
			parsed_index += utf16_length;
		}
	}

	// now let's convert the string from UTF-16 to UTF-8
	fassert(parsed_index == parsed_length);

	parsed_index = 0;
	while (parsed_index < parsed_length) {
		size_t utf16_length = 0;
		size_t codepoint_utf8_length = 0;
		fassert(simple_utf16_to_utf8(&parsed[parsed_index], parsed_length - parsed_index, 0, &utf16_length, NULL, &codepoint_utf8_length));
		parsed_index += utf16_length;
		utf8_length += codepoint_utf8_length;
	}

	status = sys_mempool_allocate(sizeof(char) * utf8_length, NULL, (void*)&result);
	if (status != ferr_ok) {
		goto out;
	}

	parsed_index = 0;
	while (parsed_index < parsed_length && utf8_index < utf8_length) {
		size_t utf16_length = 0;
		size_t codepoint_utf8_length = 0;
		fassert(simple_utf16_to_utf8(&parsed[parsed_index], parsed_length - parsed_index, utf8_length - utf8_index, &utf16_length, &result[utf8_index], &codepoint_utf8_length));
		parsed_index += utf16_length;
		utf8_index += codepoint_utf8_length;
	}

	*out_characters = offset;
	*out_string = result;
	*out_string_length = parsed_length;

out:
	if (parsed) {
		LIBJSON_WUR_IGNORE(sys_mempool_free(parsed));
	}
	return status;
};

ferr_t json_parse_string_n(const char* string, size_t string_length, bool json5, json_object_t** out_object) {
	json_object_stack_t* object_stack = NULL;
	size_t object_stack_size = 0;
	ferr_t status = ferr_ok;
	json_object_t* result = NULL;

	while (string_length > 0) {
		// skip whitespace
		while (string_length > 0 && json_isspace(string[0])) {
			++string;
			--string_length;
		}

		if (string_length == 0) {
			break;
		}

		json_object_stack_t* 

		if ()
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
