/*
 * This file is part of Anillo OS
 * Copyright (C) 2022 Anillo OS Developers
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

#include <stdint.h>
#include <stddef.h>

#include <libsimple/libsimple.h>

#include <immintrin.h>

// seems to be slow, actually.
#define USE_REP_STOSB 0

#define USE_SSE2 1

void* simple_memset(void* destination, int value, size_t n) {
	unsigned char* destbuf = destination;
	unsigned char uchar_value = (unsigned char)value;

	// regardless of which path we take, we want to start with a 16-byte aligned address,
	// so perform some initial slow assignment to ensure alignment

	while (((uintptr_t)destbuf & 0x0f) != 0 && n > 0) {
		*(destbuf++) = uchar_value;
		--n;
	}

	// likewise, we need the length to be a multiple of 16 bytes, so assign any leftover bytes the slow way
	while ((n & 0x0f) != 0 && n > 0) {
		*(destbuf + n - 1) = uchar_value;
		--n;
	}

	if (USE_REP_STOSB && n >= 256) {
		// use "rep stosb"; it's optimized for this situation
		__asm__ volatile(
			"cld\n"
			"rep stosb\n"
			:
			"+D" (destbuf),
			"+c" (n)
			:
			"a" (uchar_value)
			:
			"memory",
			"cc"
		);
	} else if (USE_SSE2) {
		// use a single SSE load and a loop of non-temporal stores
		__m128i value_vec = _mm_set1_epi8(uchar_value);

		while (n > 0) {
			_mm_stream_si128((void*)destbuf, value_vec);
			destbuf += 16;
			n -= 16;
		}

		// now synchronize the non-temporal stores
		_mm_sfence();
	} else {
		// assign in multiples of 8 bytes;
		// slow, but not as slow as a byte loop
		uint64_t big_value =
			((uint64_t)uchar_value <<  0) |
			((uint64_t)uchar_value <<  8) |
			((uint64_t)uchar_value << 16) |
			((uint64_t)uchar_value << 24) |
			((uint64_t)uchar_value << 32) |
			((uint64_t)uchar_value << 40) |
			((uint64_t)uchar_value << 48) |
			((uint64_t)uchar_value << 56)
			;
		while (n > 0) {
			*((uint64_t*)destbuf) = big_value;
			destbuf += sizeof(uint64_t);
			n -= sizeof(uint64_t);
		}
	}

	return destination;
};
