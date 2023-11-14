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

#ifndef _LIBSPOOKY_TYPES_PRIVATE_H_
#define _LIBSPOOKY_TYPES_PRIVATE_H_

#include <libspooky/types.h>
#include <libspooky/objects.private.h>

LIBSPOOKY_DECLARATIONS_BEGIN;

LIBSPOOKY_ENUM(uint8_t, spooky_type_tag) {
	spooky_type_tag_invalid         = 0,
	spooky_type_tag_u8              = 1,
	spooky_type_tag_u16             = 2,
	spooky_type_tag_u32             = 4,
	spooky_type_tag_u64             = 5,
	spooky_type_tag_i8              = 6,
	spooky_type_tag_i16             = 7,
	spooky_type_tag_i32             = 8,
	spooky_type_tag_i64             = 9,
	spooky_type_tag_bool            = 10,
	spooky_type_tag_f32             = 11,
	spooky_type_tag_f64             = 12,
	spooky_type_tag_structure       = 13,
	spooky_type_tag_data            = 14,
	spooky_type_tag_function        = 15,
	spooky_type_tag_nowait_function = 16,
	spooky_type_tag_proxy           = 17,
	spooky_type_tag_channel         = 18,
};

LIBSPOOKY_STRUCT(spooky_type_object) {
	spooky_object_t object;
	size_t byte_size;
	bool global;
};

extern const spooky_object_class_t spooky_direct_object_class_type;

LIBSPOOKY_DECLARATIONS_END;

#endif // _LIBSPOOKY_TYPES_PRIVATE_H_
