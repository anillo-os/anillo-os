/*
 * This file is part of Anillo OS
 * Copyright (C) 2025 Anillo OS Developers
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

#include <libjson/lex.private.h>

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

LIBJSON_ALWAYS_INLINE bool json_isspace(char character) {
	return character == ' ' || character == '\t' || character == '\n' || character == '\r';
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
			if ((*in_out_string_length) > 2 && ((*in_out_string)[1] == 'x' || (*in_out_string)[1] == 'X') && json_lexer_is_hex_digit((*in_out_string)[2])) {
				// hex integer
				out_token->type = json_token_type_hex_integer;
				*in_out_string += 3; // we know at least '0x' and a single hex digit are present
				while (json_lexer_is_hex_digit((*in_out_string)[0])) {
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
