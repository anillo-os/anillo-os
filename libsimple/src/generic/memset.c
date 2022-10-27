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

void* simple_memset(void* destination, int value, size_t n) {
	if (n == 0) {
		return destination;
	}

	char* destbuf = destination;
	unsigned char uchar_value = (unsigned char)value;

	// we want to start with an 8-byte aligned address (since aligned memory access is faster),
	// so perform some initial slow assignment to ensure alignment.

	while (((uintptr_t)destbuf & 0x07) != 0 && n > 0) {
		*(destbuf++) = uchar_value;
		--n;
	}

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
	while (n >= sizeof(uint64_t)) {
		*((uint64_t*)destbuf) = big_value;
		destbuf += sizeof(uint64_t);
		n -= sizeof(uint64_t);
	}

	// assign any leftover bytes the slow way
	while (n-- > 0) {
		*(destbuf++) = uchar_value;
	}

	return destination;
};

#if LIBSIMPLE_UEFI_COMPAT
	void* memset(void* destination, int value, size_t n) LIBSIMPLE_ALIAS(simple_memset);
#endif
