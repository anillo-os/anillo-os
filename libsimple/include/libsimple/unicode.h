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

#ifndef _LIBSIMPLE_UNICODE_H_
#define _LIBSIMPLE_UNICODE_H_

#include <stdint.h>

#include <ferro/error.h>
#include <libsimple/base.h>

LIBSIMPLE_DECLARATIONS_BEGIN;

/**
 * Translates a single codepoint from UTF-8 encoding into UTF-32 encoding.
 *
 * @param utf8_sequence   UTF-8 representation of the codepoint to transcode.
 * @param sequence_length Number of bytes that can be read from @p utf8_sequence.
 * @param out_utf32       A pointer where the transcoded UTF-32 codepoint will be written upon success.
 * @param out_utf8_length A pointer to a variable in which the number of bytes the UTF-8 representation occupies will be written. May be `NULL`.
 *                        If provided, it is always written to with the required number of bytes, regardless of success or failure.
 *
 * @note This function *will* successfully transcode codepoints that should never exist in UTF-8 (e.g. UTF-16 surrogates).
 *
 * @retval ferr_ok               The transcoding succeeded.
 * @retval ferr_too_small        The detected UTF-8 codepoint is longer than the provided sequence.
 * @retval ferr_invalid_argument The provided UTF-8 sequence represents an invalid codepoint.
 */
ferr_t simple_utf8_to_utf32(const char* utf8_sequence, size_t sequence_length, uint32_t* out_utf32, size_t* out_utf8_length);

/**
 * Translates a single codepoint from UTF-32 encoding into UTF-8 encoding.
 *
 * @param utf32             UTF-32 representation of the codepoint to transcode.
 * @param sequence_space    Number of bytes that can be written to @p out_utf8_sequence.
 * @param out_utf8_sequence A pointer to a buffer where the UTF-8 representation of the codepoint will be written upon success.
 * @param out_utf8_length   A pointer to a variable in which the number of bytes the UTF-8 representation occupies will be written. May be `NULL`.
 *                          If provided, it is always written to with the required number of bytes, regardless of success or failure.
 *
 * @note This function *will* successfully transcode codepoints that should never exist in UTF-32 (e.g. UTF-16 surrogates).
 *
 * @retval ferr_ok               The transcoding succeeded.
 * @retval ferr_too_small        The provided buffer is not big enough to contain the UTF-8 sequence.
 * @retval ferr_invalid_argument The provided UTF-32 value represents an invalid codepoint.
 */
ferr_t simple_utf32_to_utf8(uint32_t utf32, size_t sequence_space, char* out_utf8_sequence, size_t* out_utf8_length);

LIBSIMPLE_DECLARATIONS_END;

#endif // _LIBSIMPLE_UNICODE_H_
