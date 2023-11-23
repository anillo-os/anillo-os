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

#include <libsimple/unicode.h>

ferr_t simple_utf8_to_utf32(const char* utf8_sequence, size_t sequence_length, uint32_t* out_utf32, size_t* out_utf8_length) {
	ferr_t status = ferr_too_small;
	uint32_t utf32_char = UINT32_MAX;
	uint8_t required_length = 0;

	if (sequence_length > 0) {
		uint8_t first_char = utf8_sequence[0];

		if (first_char & 0x80) {
			if ((first_char & 0x20) == 0) {
				// 2 bytes
				required_length = 2;
				if (sequence_length < required_length) {
					status = ferr_too_small;
					goto out;
				}
				utf32_char = ((first_char & 0x1f) << 6) | (utf8_sequence[1] & 0x3f);
			} else if ((first_char & 0x10) == 0) {
				// 3 bytes
				required_length = 3;
				if (sequence_length < required_length) {
					status = ferr_too_small;
					goto out;
				}
				utf32_char = ((first_char & 0x0f) << 12) | ((utf8_sequence[1] & 0x3f) << 6) | (utf8_sequence[2] & 0x3f);
			} else if ((first_char & 0x08) == 0) {
				// 4 bytes
				required_length = 4;
				if (sequence_length < required_length) {
					status = ferr_too_small;
					goto out;
				}
				utf32_char = ((first_char & 0x07) << 18) | ((utf8_sequence[1] & 0x3f) << 12) | ((utf8_sequence[2] & 0x3f) << 6) | (utf8_sequence[3] & 0x3f);
			} else {
				// more than 4 bytes???
				status = ferr_invalid_argument;
				goto out;
			}
		} else {
			required_length = 1;
			utf32_char = first_char;
		}

		status = ferr_ok;
	}

out:
	if (out_utf8_length) {
		*out_utf8_length = required_length;
	}
	if (status == ferr_ok) {
		if (out_utf32) {
			*out_utf32 = utf32_char;
		}
	}
	return status;
};

ferr_t simple_utf32_to_utf8(uint32_t utf32, size_t sequence_space, char* out_utf8_sequence, size_t* out_utf8_length) {
	if (code_point < 0x80) {
		out_bytes[0] = code_point;
		return 1;
	} else if (code_point < 0x800) {
		out_bytes[0] = 0xc0 | ((code_point & (0x1fULL << 6)) >> 6);
		out_bytes[1] = 0x80 | (code_point & 0x3f);
		return 2;
	} else if (code_point < 0x10000) {
		out_bytes[0] = 0xe0 | ((code_point & (0x0fULL << 12)) >> 12);
		out_bytes[1] = 0x80 | ((code_point & (0x3fULL << 6)) >> 6);
		out_bytes[2] = 0x80 | (code_point & 0x3f);
		return 3;
	} else {
		out_bytes[0] = 0xf0 | ((code_point & (0x07ULL << 18)) >> 18);
		out_bytes[1] = 0x80 | ((code_point & (0x3fULL << 12)) >> 12);
		out_bytes[2] = 0x80 | ((code_point & (0x3fULL << 6)) >> 6);
		out_bytes[3] = 0x80 | (code_point & 0x3f);
		return 4;
	}
};
