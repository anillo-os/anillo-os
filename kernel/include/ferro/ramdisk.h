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

#ifndef _FERRO_RAMDISK_H_
#define _FERRO_RAMDISK_H_

#include <stdint.h>

#include <ferro/base.h>

FERRO_DECLARATIONS_BEGIN;

typedef FERRO_PACKED_STRUCT(ferro_ramdisk_header) ferro_ramdisk_header_t;
FERRO_PACKED_STRUCT(ferro_ramdisk_header) {
	// Size of the contents of the ramdisk. Does NOT include the size of this header.
	uint64_t ramdisk_size;

	// Temporary char array until we actually get something in the ramdisk.
	char contents[];
};

FERRO_DECLARATIONS_END;

#endif // _FERRO_RAMDISK_H_
