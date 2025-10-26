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
#include <libmath/libmath.h>

LIBJSON_STRUCT(json_object_stack) {
	json_object_t* object;
	char* pending_key;
	size_t pending_key_length;
};

LIBJSON_ENUM(uint8_t, json_parser_state) {
	json_parser_state_any_start,

	json_parser_state_object_key,
	json_parser_state_object_key_or_end,
	json_parser_state_object_colon,
	json_parser_state_object_value,
	json_parser_state_object_comma_or_end,

	json_parser_state_array_value,
	json_parser_state_array_value_or_end,
	json_parser_state_array_comma_or_end,

	json_parser_state_end,
};

LIBJSON_ENUM(uint8_t, json_token_type) {
	json_token_type_invalid = 0,

	json_token_type_eof,

	json_token_type_opening_brace,
	json_token_type_closing_brace,
	json_token_type_opening_square,
	json_token_type_closing_square,
	json_token_type_colon,
	json_token_type_comma,
	json_token_type_identifier,
	json_token_type_single_quote,
	json_token_type_double_quote,
	json_token_type_decimal_point,
	json_token_type_plus,
	json_token_type_minus,
	json_token_type_hex_integer,
	json_token_type_decimal_integer,
};

LIBJSON_STRUCT(json_token) {
	json_token_type_t type;
	const char* contents;
	size_t length;
};

LIBJSON_ALWAYS_INLINE bool is_hex_digit(char character) {
	return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f') || (character >= 'A' && character <= 'F');
};

LIBJSON_ALWAYS_INLINE bool is_identifier_start(char character) {
	return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') || (character == '_');
};

LIBJSON_ALWAYS_INLINE bool is_identifier_body(char character) {
	return is_identifier_start(character) || (character >= '0' && character <= '9');
};

LIBJSON_ALWAYS_INLINE bool json_is_line_terminator(uint32_t utf32) {
	switch (utf32) {
		case '\n':
		case '\r':
		case 0x2028: // Line separator
		case 0x2029: // Paragraph separator
			return true;
		default:
			return false;
	}
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

ferr_t json_parse_file(vfs_node_t* file, bool json5, json_object_t** out_object) {
	ferr_t status = ferr_ok;
	vfs_node_info_t info;
	sys_data_t* data = NULL;

	status = vfs_node_get_info(file, &info);
	if (status != ferr_ok) {
		goto out;
	}

	status = vfs_node_read_data(file, 0, info.size, &data);
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

void json_lexer_next(const char** in_out_string, size_t* in_out_string_length, bool skip_whitespace, bool skip_comments, json_token_t* out_token) {
	const char* start = NULL;

	simple_memset(out_token, 0, sizeof(*out_token));

	if (skip_comments) {
		bool check_comment = true;
		while (check_comment) {
			if (skip_whitespace) {
				// skip whitespace
				while ((*in_out_string_length) > 0 && json_isspace((*in_out_string)[0])) {
					++(*in_out_string);
					--(*in_out_string_length);
				}
			}

			check_comment = false;

			if ((*in_out_string_length) > 1) {
				if ((*in_out_string)[0] == '/' && (*in_out_string)[1] == '/') {
					// single line comment
					check_comment = true;
					*in_out_string += 2;
					*in_out_string_length -= 2;

					while (true) {
						uint32_t utf32 = 0;
						size_t utf8_length = 0;

						if (simple_utf8_to_utf32(*in_out_string, *in_out_string_length, &utf8_length, &utf32) != ferr_ok) {
							break;
						}

						*in_out_string += utf8_length;
						*in_out_string_length -= utf8_length;

						if (utf32 == '\r' && (*in_out_string_length) > 0 && (*in_out_string)[0] == '\n') {
							++(*in_out_string);
							--(*in_out_string_length);
						}

						if (json_is_line_terminator(utf32)) {
							break;
						}
					}
				} else if ((*in_out_string)[0] == '/' && (*in_out_string)[1] == '*') {
					check_comment = true;
					*in_out_string += 2;
					*in_out_string_length -= 2;

					while (true) {
						if (*in_out_string_length < 2) {
							// the comment implicitly lasts for the rest of the string, since the comment terminator sequence is 2 characters
							*in_out_string += *in_out_string_length;
							*in_out_string_length = 0;
							break;
						}

						// here, we know the string is at least 2 characters long
						char first = (*in_out_string)[0];
						char second = (*in_out_string)[1];

						if (second == '*') {
							// we can only advance by a single character for the next iteration
							// since we might have a termination sequence (`*/`)
							*in_out_string += 1;
							*in_out_string_length -= 1;
						} else {
							*in_out_string += 2;
							*in_out_string_length -= 2;
						}

						if (first == '*' && second == '/') {
							break;
						}
					}
				}
			}

			if ((*in_out_string_length) == 0) {
				out_token->type = json_token_type_eof;
				return;
			}
		}
	}

	if (skip_whitespace) {
		// skip whitespace
		while ((*in_out_string_length) > 0 && json_isspace((*in_out_string)[0])) {
			++(*in_out_string);
			--(*in_out_string_length);
		}
	}

	if ((*in_out_string_length) == 0) {
		out_token->type = json_token_type_eof;
		return;
	}

	start = *in_out_string;

	switch ((*in_out_string)[0]) {
		case '{':
			out_token->type = json_token_type_opening_brace;
			++(*in_out_string);
			break;
		case '}':
			out_token->type = json_token_type_closing_brace;
			++(*in_out_string);
			break;
		case '[':
			out_token->type = json_token_type_opening_square;
			++(*in_out_string);
			break;
		case ']':
			out_token->type = json_token_type_closing_square;
			++(*in_out_string);
			break;
		case ':':
			out_token->type = json_token_type_colon;
			++(*in_out_string);
			break;
		case ',':
			out_token->type = json_token_type_comma;
			++(*in_out_string);
			break;
		case '.':
			out_token->type = json_token_type_decimal_point;
			++(*in_out_string);
			break;
		case '+':
			out_token->type = json_token_type_plus;
			++(*in_out_string);
			break;
		case '-':
			out_token->type = json_token_type_minus;
			++(*in_out_string);
			break;

		// for these, we do *not* consume the character; they are consumed by `json_parse_string_object`
		case '\'':
			out_token->type = json_token_type_single_quote;
			break;
		case '"':
			out_token->type = json_token_type_double_quote;
			break;

		case '0':
			// this is either '0' by itself (decimal integer) or '0x1234abcd...' (hex integer)
			if ((*in_out_string_length) > 2 && ((*in_out_string)[1] == 'x' || (*in_out_string)[1] == 'X') && is_hex_digit((*in_out_string)[2])) {
				// hex integer
				out_token->type = json_token_type_hex_integer;
				*in_out_string += 3; // we know at least '0x' and a single hex digit are present
				while (is_hex_digit((*in_out_string)[0])) {
					++(*in_out_string);
				}
			} else {
				// just 0
				out_token->type = json_token_type_decimal_integer;
				++(*in_out_string);
			}
			break;

		default: {
			// let's try the multi-character tokens
			if ((*in_out_string)[0] >= '1' && (*in_out_string)[0] <= '9') {
				// decimal integer
				out_token->type = json_token_type_decimal_integer;
				++(*in_out_string);
				while ((*in_out_string)[0] >= '1' && (*in_out_string)[0] <= '9') {
					++(*in_out_string);
				}
			} else if (is_identifier_start((*in_out_string)[0])) {
				out_token->type = json_token_type_identifier;
				++(*in_out_string);
				while (is_identifier_body((*in_out_string)[0])) {
					++(*in_out_string);
				}
			}
		} break;
	}

	if (start && out_token->type != json_token_type_invalid) {
		out_token->contents = start;
		out_token->length = (*in_out_string) - start;
		*in_out_string_length -= out_token->length;
	}
};

void json_lexer_peek(const char* string, size_t length, bool skip_whitespace, bool skip_comments, json_token_t* out_token) {
	json_lexer_next(&string, &length, skip_whitespace, skip_comments, out_token);
};

void json_lexer_consume_peek(json_token_t* in_token, const char** in_out_string, size_t* in_out_string_length) {
	if (*in_out_string >= (in_token->contents + in_token->length)) {
		return;
	}

	size_t skipped_len = (in_token->contents + in_token->length) - *in_out_string;
	*in_out_string += skipped_len;
	*in_out_string_length -= skipped_len;
};

ferr_t json_parse_string_n(const char* string, size_t string_length, bool json5, json_object_t** out_object) {
	json_object_stack_t* object_stack = NULL;
	size_t object_stack_size = 0;
	bool need_new_object = true;
	ferr_t status = ferr_ok;
	json_object_t* result = NULL;

continue_loop:
	while (string_length > 0 && (need_new_object || object_stack_size != 0)) {
		json_object_stack_t* current_object = (!need_new_object && object_stack_size > 0) ? (&object_stack[object_stack_size - 1]) : NULL;

		if (need_new_object) {
			// we can parse the beginning of any object

			need_new_object = false;

			json_token_t token;
			json_lexer_next(&string, &string_length, true, json5, &token);

			switch (token.type) {
				case json_token_type_opening_brace:
				case json_token_type_opening_square: {
					// let's create an entry on the stack
					status = sys_mempool_reallocate(object_stack, sizeof(*object_stack) * (object_stack_size + 1), NULL, (void*)&object_stack);
					if (status != ferr_ok) {
						goto out;
					}

					++object_stack_size;

					current_object = &object_stack[object_stack_size - 1];
					simple_memset(current_object, 0, sizeof(*current_object));

					if (token.type == json_token_type_opening_brace) {
						status = json_dict_new(0, NULL, NULL, NULL, &current_object->object);
					} else {
						status = json_array_new(0, NULL, &current_object->object);
					}
					if (status != ferr_ok) {
						goto out;
					}
				} break;

				case json_token_type_single_quote:
				case json_token_type_double_quote: {
					size_t consumed_chars = 0;
					char* parsed = NULL;
					size_t parsed_length = 0;

					status = json_parse_string_object(string, string_length, json5, &consumed_chars, &parsed, &parsed_length);
					if (status != ferr_ok) {
						goto out;
					}

					string += consumed_chars;
					string_length -= consumed_chars;

					status = json_string_new_n(parsed, parsed_length, &result);
					if (status != ferr_ok) {
						goto out;
					}

					LIBJSON_WUR_IGNORE(sys_mempool_free(parsed));
				} break;

				case json_token_type_decimal_integer:
				case json_token_type_hex_integer:
				case json_token_type_plus:
				case json_token_type_minus:
				case json_token_type_decimal_point: {
					bool negative = false;
					bool negative_exponent = false;
					bool found_whole_part = false;
					bool found_fraction_part = false;
					bool found_exponent_part = false;
					bool found_decimal_point = false;
					uintmax_t whole_part = 0;
					uintmax_t fraction_part = 0;
					uintmax_t exponent_part = 1;
					double val = 0;

					if (token.type == json_token_type_plus) {
						if (!json5) {
							status = ferr_invalid_argument;
							goto out;
						}
						json_lexer_next(&string, &string_length, true, json5, &token);
					} else if (token.type == json_token_type_minus) {
						negative = true;
						json_lexer_next(&string, &string_length, true, json5, &token);
					}

					// this can only occur if we had a `+` or `-`
					if (token.type == json_token_type_identifier) {
						if (!json5) {
							status = ferr_invalid_argument;
							goto out;
						}

						if (token.length == 8 && simple_strncmp(token.contents, "Infinity", 8) == 0) {
							status = json_number_new_float(negative ? (-__builtin_inf()) : __builtin_inf(), &result);
						} else if (token.length == 3 && simple_strncmp(token.contents, "NaN", 3) == 0) {
							status = json_number_new_float(negative ? (-__builtin_nan("0")) : __builtin_nan("0"), &result);
						}

						if (status != ferr_ok) {
							goto out;
						}

						if (!result) {
							status = ferr_invalid_argument;
							goto out;
						}

						goto continue_loop;
					}

					if (token.type == json_token_type_hex_integer) {
						if (!json5) {
							status = ferr_invalid_argument;
							goto out;
						}

						uintmax_t value;
						status = simple_string_to_integer_unsigned(&token.contents[2], token.length - 2, NULL, 16, &value);
						if (status != ferr_ok) {
							goto out;
						}

						if (negative) {
							status = json_number_new_signed_integer(-(int64_t)value, &result);
						} else {
							status = json_number_new_unsigned_integer(value, &result);
						}

						if (status != ferr_ok) {
							goto out;
						}

						goto continue_loop;
					}

					if (token.type == json_token_type_decimal_integer) {
						found_whole_part = true;
						status = simple_string_to_integer_unsigned(token.contents, token.length, NULL, 10, &whole_part);
						if (status != ferr_ok) {
							goto out;
						}

						val += (double)whole_part;

						json_lexer_peek(string, string_length, false, false, &token);
					}

					if (token.type == json_token_type_decimal_point) {
						if (!json5 && !found_whole_part) {
							status = ferr_invalid_argument;
							goto out;
						}

						if (found_whole_part) {
							// this was a peek; consume it
							json_lexer_consume_peek(&token, &string, &string_length);
						}
						found_decimal_point = true;

						json_lexer_peek(string, string_length, false, false, &token);
						if ((!json5 || !found_whole_part) && token.type != json_token_type_decimal_integer) {
							// only JSON5 allows trailing decimal points...
							// ...but also, under JSON5, if we have a leading decimal point, we MUST have a fraction part
							status = ferr_invalid_argument;
							goto out;
						}

						if (token.type == json_token_type_decimal_integer) {
							// consume it
							json_lexer_consume_peek(&token, &string, &string_length);
							found_fraction_part = true;

							status = simple_string_to_integer_unsigned(token.contents, token.length, NULL, 10, &fraction_part);
							if (status != ferr_ok) {
								goto out;
							}

							val += (double)fraction_part / (double)math_pow_u64(10, token.length, NULL);

							json_lexer_peek(string, string_length, false, false, &token);
						}
					}

					if (token.type == json_token_type_identifier && token.length >= 1 && (token.contents[0] == 'e' || token.contents[0] == 'E')) {
						if (!found_whole_part && !found_fraction_part) {
							status = ferr_invalid_argument;
							goto out;
						}

						// consume only the first character
						size_t skip_count = (token.contents + 1) - string;
						string += skip_count;
						string_length -= skip_count;
						found_exponent_part = true;

						// this *has* to be followed by a decimal integer for the exponent value (with an optional plus or minus)
						json_lexer_next(&string, &string_length, false, false, &token);
						if (token.type == json_token_type_plus) {
							negative_exponent = false;
							json_lexer_next(&string, &string_length, false, false, &token);
						} else if (token.type == json_token_type_minus) {
							negative_exponent = true;
							json_lexer_next(&string, &string_length, false, false, &token);
						}
						if (token.type != json_token_type_decimal_integer) {
							status = ferr_invalid_argument;
							goto out;
						}

						status = simple_string_to_integer_unsigned(token.contents, token.length, NULL, 10, &exponent_part);
						if (status != ferr_ok) {
							goto out;
						}

						if (negative_exponent) {
							val *= math_pow_di(10, -(int64_t)exponent_part, NULL);
						} else {
							val *= (double)math_pow_u64(10, exponent_part, NULL);
						}
					}

					if (!found_whole_part && !found_fraction_part && !found_exponent_part) {
						status = ferr_invalid_argument;
						goto out;
					}

					if (negative) {
						val *= -1;
					}

					if (found_fraction_part || found_exponent_part) {
						status = json_number_new_float(val, &result);
					} else if (negative) {
						status = json_number_new_signed_integer(-(int64_t)whole_part, &result);
					} else {
						status = json_number_new_unsigned_integer(whole_part, &result);
					}

					if (status != ferr_ok) {
						goto out;
					}
				} break;

				case json_token_type_identifier: {
					// this could be few different things
					if (token.length == 4) {
						if (simple_strncmp(token.contents, "true", 4) == 0) {
							result = json_bool_new(true);
						} else if (simple_strncmp(token.contents, "null", 4) == 0) {
							result = json_null_new();
						}
					} else if (token.length == 5 && simple_strncmp(token.contents, "false", 5) == 0) {
						result = json_bool_new(false);
					} else if (token.length == 8 && simple_strncmp(token.contents, "Infinity", 8) == 0) {
						status = json_number_new_float(__builtin_inf(), &result);
						if (status != ferr_ok) {
							goto out;
						}
					} else if (token.length == 3 && simple_strncmp(token.contents, "NaN", 3) == 0) {
						status = json_number_new_float(__builtin_nan("0"), &result);
						if (status != ferr_ok) {
							goto out;
						}
					}

					if (!result) {
						status = ferr_invalid_argument;
						goto out;
					}
				} break;

				default:
					status = ferr_invalid_argument;
					goto out;
			}
		} else {
			bool is_dict;
			json_token_t token;

			if (json_object_class(current_object->object) == json_object_class_dict()) {
				is_dict = true;
			} else {
				fassert(json_object_class(current_object->object) == json_object_class_array());
				is_dict = false;
			}

			json_lexer_peek(string, string_length, true, json5, &token);

			if (!result) {
				// this means we're on the first iteration

				if ((is_dict && token.type == json_token_type_closing_brace) || (!is_dict && token.type == json_token_type_closing_square)) {
					// alright, this is an empty dictionary/array

					// consume the closing brace/square
					json_lexer_consume_peek(&token, &string, &string_length);

					result = current_object->object;
					--object_stack_size;
					// try to shrink the stack, but ignore any failures
					LIBJSON_WUR_IGNORE(sys_mempool_reallocate(object_stack, sizeof(*object_stack) * object_stack_size, NULL, (void*)&object_stack));
					continue;
				}
			} else {
				bool had_comma = false;

				// we're not on the first iteration, so we need to 1) assign the produced value to our object, and 2) check for a comma before accepting another entry

				if (is_dict) {
					status = json_dict_set_n(current_object->object, current_object->pending_key, current_object->pending_key_length, result);
				} else {
					status = json_array_append(current_object->object, result);
				}
				if (status != ferr_ok) {
					goto out;
				}

				if (current_object->pending_key) {
					LIBJSON_WUR_IGNORE(sys_mempool_free(current_object->pending_key));

					current_object->pending_key = NULL;
					current_object->pending_key_length = 0;
				}
				result = NULL;

				if (token.type == json_token_type_comma) {
					// consume it
					json_lexer_consume_peek(&token, &string, &string_length);

					json_lexer_peek(string, string_length, true, json5, &token);

					had_comma = true;
				}

				if ((is_dict && token.type == json_token_type_closing_brace) || (!is_dict && token.type == json_token_type_closing_square)) {
					if (!json5 && had_comma) {
						// trailing commas are only allowed in JSON5
						status = ferr_invalid_argument;
						goto out;
					}

					// consume it
					json_lexer_consume_peek(&token, &string, &string_length);

					json_lexer_peek(string, string_length, true, json5, &token);

					result = current_object->object;
					--object_stack_size;
					// try to shrink the stack, but ignore any failures
					LIBJSON_WUR_IGNORE(sys_mempool_reallocate(object_stack, sizeof(*object_stack) * object_stack_size, NULL, (void*)&object_stack));
					continue;
				} else if (!had_comma) {
					// if we didn't find a comma, we HAD to find a closing brace/square
					status = ferr_invalid_argument;
					goto out;
				}
			}

			if (is_dict) {
				// we only peeked at the token before; when we get here, we MUST have a token, so let's consume it
				json_lexer_consume_peek(&token, &string, &string_length);

				if (token.type == json_token_type_single_quote || token.type == json_token_type_double_quote) {
					size_t consumed_chars = 0;
					status = json_parse_string_object(string, string_length, json5, &consumed_chars, &current_object->pending_key, &current_object->pending_key_length);
					if (status != ferr_ok) {
						goto out;
					}

					string += consumed_chars;
					string_length -= consumed_chars;
				} else if (token.type == json_token_type_identifier) {
					if (!json5) {
						// only JSON5 allows identifiers as keys
						status = ferr_invalid_argument;
						goto out;
					}

					status = sys_mempool_allocate(token.length, NULL, (void*)&current_object->pending_key);
					if (status != ferr_ok) {
						goto out;
					}

					simple_memcpy(current_object->pending_key, token.contents, token.length);
					current_object->pending_key_length = token.length;
				} else {
					status = ferr_invalid_argument;
					goto out;
				}

				json_lexer_next(&string, &string_length, true, json5, &token);

				if (token.type != json_token_type_colon) {
					status = ferr_invalid_argument;
					goto out;
				}
			}

			need_new_object = true;
		}
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
	if (status != ferr_ok) {
		if (result) {
			LIBJSON_WUR_IGNORE(json_release(result));
		}
	}
	return status;
};
