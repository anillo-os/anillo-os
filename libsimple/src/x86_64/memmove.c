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
#define USE_REP_MOVSB_HIGH_TO_LOW 0

#define USE_SSE2 1

void* simple_memmove(void* destination, const void* source, size_t n) {
	if (destination == source || n == 0) {
		return destination;
	}

	char* destbuf = destination;
	const char* srcbuf = source;
	bool misaligned = ((uintptr_t)destbuf & 0x0f) != ((uintptr_t)srcbuf & 0x0f);

	if (destbuf < srcbuf) {
		if (destbuf + n < srcbuf) {
			// the two regions don't actually overlap; upgrade to a memcpy
			return simple_memcpy(destination, source, n);
		}

		// this is essentially just memcpy, but we can't call it because it has the "restrict" modifier on its pointer arguments,
		// and we don't want to extract the logic into a separate function because then the compiler can't perform additional
		// optimizations for memcpy using the "restrict" modifiers. we also don't want to extract it into a macro, because
		// that makes it significantly harder to debug, since we lose line number information.

		//
		// see the notes in memcpy about misaligned arguments.
		//

		// perform some initial slow copying to ensure alignment
		while (((uintptr_t)destbuf & 0x0f) != 0 && n > 0) {
			*(destbuf++) = *(srcbuf++);
			--n;
		}

		// note that, unlike memcpy, we can't perform non-sequential copies;
		// thus, we cannot ensure the length is a multiple of 16 bytes here.

		if (USE_REP_MOVSB_HIGH_TO_LOW && !misaligned && n >= 256) {
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
			while (n >= 16) {
				_mm_stream_si128((void*)destbuf, _mm_load_si128((const void*)srcbuf));
				destbuf += 16;
				srcbuf += 16;
				n -= 16;
			}

			// now synchronize the non-temporal stores
			_mm_sfence();
		} else if (USE_SSE2 && misaligned) {
			// use unaligned SSE2 loads and aligned non-temporal stores
			while (n >= 16) {
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
			while (n >= sizeof(uint64_t)) {
				*((uint64_t*)destbuf) = *((const uint64_t*)srcbuf);
				destbuf += sizeof(uint64_t);
				srcbuf += sizeof(uint64_t);
				n -= sizeof(uint64_t);
			}
		}

		// now copy any leftover bytes the slow way
		while (n-- > 0) {
			*(destbuf++) = *(srcbuf++);
		}
	} else {
		if (srcbuf + n < destbuf) {
			// the two regions don't actually overlap; upgrade to a memcpy
			return simple_memcpy(destination, source, n);
		}

		destbuf = destbuf + n;
		srcbuf = srcbuf + n;

		// this is essentially just memcpy, but in reverse

		// perform some initial slow copying to ensure alignment
		while (((uintptr_t)destbuf & 0x0f) != 0 && n > 0) {
			*(--destbuf) = *(--srcbuf);
			--n;
		}

		// note that, unlike memcpy, we can't perform non-sequential copies;
		// thus, we cannot ensure the length is a multiple of 16 bytes here.

		// note that we do NOT try to use "rep movsb" in this case;
		// it's not optimized for copying in reverse (from high to low).

		if (USE_SSE2 && !misaligned) {
			// use SSE2 loads and non-temporal stores
			while (n >= 16) {
				destbuf -= 16;
				srcbuf -= 16;
				_mm_stream_si128((void*)destbuf, _mm_load_si128((const void*)srcbuf));
				n -= 16;
			}

			// now synchronize the non-temporal stores
			_mm_sfence();
		} else if (USE_SSE2 && misaligned) {
			// use unaligned SSE2 loads and aligned non-temporal stores
			while (n >= 16) {
				destbuf -= 16;
				srcbuf -= 16;
				_mm_stream_si128((void*)destbuf, _mm_loadu_si128((const void*)srcbuf));
				n -= 16;
			}

			// now synchronize the non-temporal stores
			_mm_sfence();
		} else {
			// no actual alignment requirements on this slow path, just *slightly* slower if misaligned.
			// (but modern processors should be able to handle unaligned memory access almost as fast as aligned memory accesses)

			// copy in multiples of 8 bytes;
			// slow, but not as slow as a byte loop
			while (n >= sizeof(uint64_t)) {
				destbuf -= sizeof(uint64_t);
				srcbuf -= sizeof(uint64_t);
				*((uint64_t*)destbuf) = *((const uint64_t*)srcbuf);
				n -= sizeof(uint64_t);
			}
		}

		// now copy any leftover bytes the slow way
		while (n-- > 0) {
			*(--destbuf) = *(--srcbuf);
		}
	}

	return destination;
};
