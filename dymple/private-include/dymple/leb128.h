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

#ifndef _DYMPLE_LEB128_H_
#define _DYMPLE_LEB128_H_

#include <stdint.h>
#include <stddef.h>

#include <dymple/base.h>
#include <ferro/error.h>

DYMPLE_DECLARATIONS_BEGIN;

// Little Endian Base-128 decoding
// Based on pseudocode from https://en.wikipedia.org/wiki/LEB128

DYMPLE_ALWAYS_INLINE ferr_t dymple_leb128_decode_unsigned(const void* bytes, size_t max_bytes, uintmax_t* out_result, size_t* out_byte_count) {
	uintmax_t result = 0;
	const char* actual_bytes = bytes;
	size_t i = 0;

	for (; i < max_bytes; ++i) {
		// put the data at the correct offset
		result |= (uintmax_t)(actual_bytes[i] & 0x7f) << (i * 7);

		// check if we're done
		if ((actual_bytes[i] >> 7) == 0) {
			++i;
			break;
		}

		// if there are still more bits and we've reached the maximum for the result, report an overflow
		if (i * 7 > sizeof(result) * 8) {
			return ferr_too_big;
		}
	}

	if (out_result) {
		*out_result = result;
	}
	if (out_byte_count) {
		*out_byte_count = i;
	}
	return ferr_ok;
};

DYMPLE_ALWAYS_INLINE ferr_t dymple_leb128_decode_signed(const void* bytes, size_t max_bytes, intmax_t* out_result, size_t* out_byte_count) {
	intmax_t result = 0;
	const char* actual_bytes = bytes;
	size_t i = 0;

	for (; i < max_bytes; ++i) {
		// put the data at the correct offset
		result |= (uintmax_t)(actual_bytes[i] & 0x7f) << (i * 7);

		// check if we're done
		if ((actual_bytes[i] >> 7) == 0) {
			++i;
			break;
		}

		// if there are still more bits and we've reached the maximum for the result, report an overflow
		if (i * 7 > sizeof(result) * 8) {
			return ferr_too_big;
		}
	}

	// sign-extend the number (if necessary)
	// the second-to-highest bit (i.e. bit 6) is the sign bit
	if (i > 0 && actual_bytes[i - 1] & 0x40) {
		// this will OR the topmost bits as 1 and leave the bottommost ((i - 1) * 7) bits alone
		result |= ~UINTMAX_C(0) << (i - 1) * 7;
	}

	if (out_result) {
		*out_result = result;
	}
	if (out_byte_count) {
		*out_byte_count = i;
	}
	return ferr_ok;
};

DYMPLE_DECLARATIONS_END;

#endif // _DYMPLE_LEB128_H_
