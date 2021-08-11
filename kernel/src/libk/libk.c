/**
 * This file is part of Anillo OS
 * Copyright (C) 2020 Anillo OS Developers
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
//
// libk.c
//
// minimalistic library for kernel-space utilities
//

#include <stdint.h>

#include <libk/libk.h>

void* memcpy(void* restrict destination, const void* restrict source, size_t n) {
	if (destination == source) {
		return destination;
	}
	char* destbuf = destination;
	const char* srcbuf = source;
	while (n-- > 0) {
		*(destbuf++) = *(srcbuf++);
	}
	return destination;
};

void* memclone(void* restrict destination, const void* restrict source, size_t n, size_t m) {
	char* destbuf = destination;
	while (m-- > 0) {
		memcpy(destbuf, source, n);
		destbuf += n;
	}
	return destination;
};

size_t strlen(const char* string) {
	size_t count = 0;
	while (*(string++) != '\0')
		++count;
	return count;
};

void* memmove(void* destination, const void* source, size_t n) {
	if (destination == source || n == 0) {
		return destination;
	} else if (destination < source) {
		char* destbuf = destination;
		const char* srcbuf = source;
		while (n-- > 0)
			*(destbuf++) = *(srcbuf++);
	} else if (destination > source) {
		char* destbuf = (char*)destination + n - 1;
		const char* srcbuf = (char*)source + n - 1;
		while (n-- > 0)
			*(destbuf--) = *(srcbuf--);
	}
	return destination;
};

void* memset(void* destination, int value, size_t n) {
	unsigned char* destbuf = destination;
	while (n-- > 0)
		*(destbuf++) = (unsigned char)value;
	return destination;
};

int strncmp(const char* first, const char* second, size_t n) {
	while (n-- > 0) {
		char first_char = *(first++);
		char second_char = *(second++);

		if (first_char < second_char) {
			return -1;
		} else if (first_char > second_char) {
			return 1;
		} else if (first_char == '\0') {
			return 0;
		}
	}

	return 0;
};

int memcmp(const void* _first, const void* _second, size_t n) {
	const uint8_t* first = _first;
	const uint8_t* second = _second;

	while (n-- > 0) {
		uint8_t first_char = *(first++);
		uint8_t second_char = *(second++);

		if (first_char < second_char) {
			return -1;
		} else if (first_char > second_char) {
			return 1;
		}
	}

	return 0;
};
