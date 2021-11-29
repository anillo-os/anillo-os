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

#include <string.h>
#include <stdint.h>

char* strncpy(char* restrict destination, const char* restrict source, size_t count) {
	size_t i = 0;

	for (; i < count; ++i) {
		if (*source == '\0') {
			break;
		}
		destination[i] = *(source++);
	}

	memset(&destination[i], 0, count - i);

	return destination;
};

char* strcpy(char* restrict destination, const char* restrict source) {
	char* orig = destination;

	while (*source != '\0') {
		*(destination++) = *(source++);
	}

	*destination = '\0';

	return orig;
};
