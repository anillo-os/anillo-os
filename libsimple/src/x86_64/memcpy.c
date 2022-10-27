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
#include <stdbool.h>

#include <libsimple/libsimple.h>

#include <immintrin.h>

// seems to be slow, actually.
#define USE_REP_MOVSB 0

#define USE_SSE2 1

void* simple_memcpy(void* restrict destination, const void* restrict source, size_t n) {
	if (destination == source) {
		return destination;
	}

	char* destbuf = destination;
	const char* srcbuf = source;
	bool misaligned = ((uintptr_t)destbuf & 0x0f) != ((uintptr_t)srcbuf & 0x0f);

	// if the 2 address are out of alignment with each other (with respect to 16-byte alignment),
	// there's no way we can perform any sort of aligned copy with them.
	//
	// fallback to doing an unaligned copy.

	// if the 2 addresses are aligned with each other, we can use a few more optimizations.
	// they may not be 16-byte aligned yet, but at least we can make them 16-byte aligned.

	// in the misaligned case, we still try to align at least the destination buffer so we can do some SSE ops.
	// we can deal with loading unaligned addresses with SSE, but we prefer to store aligned addresses.
	// (so that we can use non-temporal stores)

	// perform some initial slow copying to ensure alignment
	while (((uintptr_t)destbuf & 0x0f) != 0 && n > 0) {
		*(destbuf++) = *(srcbuf++);
		--n;
	}

	// likewise, we need the length to be a multiple of 16 bytes, so copy any leftover bytes the slow way
	while ((n & 0x0f) != 0 && n > 0) {
		*(destbuf + n - 1) = *(srcbuf + n - 1);
		--n;
	}

	if (USE_REP_MOVSB && !misaligned && n >= 256) {
		// use "rep movsb"; it's optimized for this situation
		__asm__ volatile(
			"cld\n"
			"rep movsb\n"
			:
			"+D" (destbuf),
			"+S" (srcbuf),
			"+c" (n)
			::
			"memory",
			"cc"
		);
	} else if (USE_SSE2 && !misaligned) {
		// use SSE2 loads and non-temporal stores
		while (n > 0) {
			_mm_stream_si128((void*)destbuf, _mm_load_si128((const void*)srcbuf));
			destbuf += 16;
			srcbuf += 16;
			n -= 16;
		}

		// now synchronize the non-temporal stores
		_mm_sfence();
	} else if (USE_SSE2 && misaligned) {
		// use unaligned SSE2 loads and aligned non-temporal stores
		while (n > 0) {
			_mm_stream_si128((void*)destbuf, _mm_loadu_si128((const void*)srcbuf));
			destbuf += 16;
			srcbuf += 16;
			n -= 16;
		}

		// now synchronize the non-temporal stores
		_mm_sfence();
	} else {
		// no actual alignment requirements on this slow path, just *slightly* slower if misaligned.
		// (but modern processors should be able to handle unaligned memory access almost as fast as aligned memory accesses)

		// copy in multiples of 8 bytes;
		// slow, but not as slow as a byte loop
		while (n > 0) {
			*((uint64_t*)destbuf) = *((const uint64_t*)srcbuf);
			destbuf += sizeof(uint64_t);
			srcbuf += sizeof(uint64_t);
			n -= sizeof(uint64_t);
		}
	}

	return destination;
};
