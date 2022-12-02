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

#ifndef _LIBSPOOKY_TYPES_H_
#define _LIBSPOOKY_TYPES_H_

#include <libspooky/base.h>
#include <libspooky/objects.h>

LIBSPOOKY_DECLARATIONS_BEGIN;

#define LIBSPOOKY_TYPE_CLASS(_name) LIBSPOOKY_OBJECT_CLASS(_name)

LIBSPOOKY_OBJECT_CLASS(type);

spooky_type_t* spooky_type_u8(void);
spooky_type_t* spooky_type_u16(void);
spooky_type_t* spooky_type_u32(void);
spooky_type_t* spooky_type_u64(void);

spooky_type_t* spooky_type_i8(void);
spooky_type_t* spooky_type_i16(void);
spooky_type_t* spooky_type_i32(void);
spooky_type_t* spooky_type_i64(void);

spooky_type_t* spooky_type_bool(void);

spooky_type_t* spooky_type_f32(void);
spooky_type_t* spooky_type_f64(void);

FERRO_WUR ferr_t spooky_retain_object_with_type(const void* object, spooky_type_t* type);
void spooky_release_object_with_type(const void* object, spooky_type_t* type);

LIBSPOOKY_DECLARATIONS_END;

#endif // _LIBSPOOKY_TYPES_H_
