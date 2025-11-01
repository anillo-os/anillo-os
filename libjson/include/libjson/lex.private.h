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

#ifndef _LIBJSON_LEX_PRIVATE_H_
#define _LIBJSON_LEX_PRIVATE_H_

#include <libjson/base.h>

LIBJSON_DECLARATIONS_BEGIN;

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

LIBJSON_ALWAYS_INLINE bool json_lexer_is_hex_digit(char character) {
	return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f') || (character >= 'A' && character <= 'F');
};

void json_lexer_next(const char** in_out_string, size_t* in_out_string_length, bool skip_whitespace, bool skip_comments, json_token_t* out_token);
void json_lexer_peek(const char* string, size_t length, bool skip_whitespace, bool skip_comments, json_token_t* out_token);
void json_lexer_consume_peek(json_token_t* in_token, const char** in_out_string, size_t* in_out_string_length);

LIBJSON_DECLARATIONS_END;

#endif // _LIBJSON_LEX_PRIVATE_H_
