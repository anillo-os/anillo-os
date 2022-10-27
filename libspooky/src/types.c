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

#include <libspooky/types.private.h>

static ferr_t spooky_type_retain(spooky_object_t* obj) {
	spooky_type_object_t* type = (void*)obj;
	if (type->global) {
		return ferr_ok;
	}
	return sys_object_retain(obj);
};

static void spooky_type_release(spooky_object_t* obj) {
	spooky_type_object_t* type = (void*)obj;
	if (type->global) {
		return;
	}
	return sys_object_release(obj);
};

const spooky_object_class_t spooky_direct_object_class_type = {
	LIBSYS_OBJECT_CLASS_INTERFACE(NULL),
	.retain = spooky_type_retain,
	.release = spooky_type_release,
};

const spooky_object_class_t* spooky_object_class_type(void) {
	return &spooky_direct_object_class_type;
};

#define SPOOKY_TYPE_DEF(_type, _typecode) \
	static spooky_type_object_t spooky_global_type_ ## _typecode = { \
		.object = { \
			.object_class = &spooky_direct_object_class_type, \
			.reference_count = 0, \
			.flags = 0, \
		}, \
		.byte_size = sizeof(_type), \
		.global = true, \
	}; \
	spooky_type_t* spooky_type_ ## _typecode(void) { \
		return (void*)&spooky_global_type_ ## _typecode; \
	};

SPOOKY_TYPE_DEF(uint8_t, u8);
SPOOKY_TYPE_DEF(uint16_t, u16);
SPOOKY_TYPE_DEF(uint32_t, u32);
SPOOKY_TYPE_DEF(uint64_t, u64);

SPOOKY_TYPE_DEF(int8_t, i8);
SPOOKY_TYPE_DEF(int16_t, i16);
SPOOKY_TYPE_DEF(int32_t, i32);
SPOOKY_TYPE_DEF(int64_t, i64);

SPOOKY_TYPE_DEF(bool, bool);

SPOOKY_TYPE_DEF(float, f32);
SPOOKY_TYPE_DEF(double, f64);
