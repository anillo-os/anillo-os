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

void* simple_memmove(void* destination, const void* source, size_t n) {
	if (destination == source || n == 0) {
		return destination;
	}

	char* destbuf = destination;
	const char* srcbuf = source;

	if (destbuf < srcbuf) {
		if (destbuf + n < srcbuf) {
			// the two regions don't actually overlap; upgrade to a memcpy
			return simple_memcpy(destination, source, n);
		}

		// this is essentially just memcpy, but we can't call it because it has the "restrict" modifier on its pointer arguments

		// we want to start with an 8-byte aligned address (since aligned memory access is faster),
		// so perform some initial slow copying to ensure alignment
		//
		// note that this is only best-effort; the two addresses may be out of alignment with each other,
		// so we may still end up with an unaligned source address. that's okay, though.

		while (((uintptr_t)destbuf & 0x07) != 0 && n > 0) {
			*(destbuf++) = *(srcbuf++);
			--n;
		}

		// copy in multiples of 8 bytes;
		// slow, but not as slow as a byte loop
		while (n > 8) {
			*((uint64_t*)destbuf) = *((const uint64_t*)srcbuf);
			destbuf += sizeof(uint64_t);
			srcbuf += sizeof(uint64_t);
			n -= sizeof(uint64_t);
		}

		// copy any leftover bytes the slow way
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

		// we want to start with an 8-byte aligned address (since aligned memory access is faster),
		// so perform some initial slow copying to ensure alignment
		//
		// note that this is only best-effort; the two addresses may be out of alignment with each other,
		// so we may still end up with an unaligned source address. that's okay, though.

		while (((uintptr_t)destbuf & 0x07) != 0 && n > 0) {
			*(--destbuf) = *(--srcbuf);
			--n;
		}

		// copy in multiples of 8 bytes;
		// slow, but not as slow as a byte loop
		while (n >= sizeof(uint64_t)) {
			destbuf -= sizeof(uint64_t);
			srcbuf -= sizeof(uint64_t);
			*((uint64_t*)destbuf) = *((const uint64_t*)srcbuf);
			n -= sizeof(uint64_t);
		}

		// copy any leftover bytes the slow way
		while (n-- > 0) {
			*(--destbuf) = *(--srcbuf);
		}
	}

	return destination;
};
