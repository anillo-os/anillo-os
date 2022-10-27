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

#ifndef _LIBSPOOKY_STRUCTURE_PRIVATE_H_
#define _LIBSPOOKY_STRUCTURE_PRIVATE_H_

#include <libspooky/structure.h>
#include <libspooky/types.private.h>

LIBSPOOKY_DECLARATIONS_BEGIN;

LIBSPOOKY_STRUCT(spooky_structure_object) {
	spooky_type_object_t base;
	size_t member_count;
	spooky_structure_member_t members[];
};

LIBSPOOKY_DECLARATIONS_END;

#endif // _LIBSPOOKY_STRUCTURE_PRIVATE_H_