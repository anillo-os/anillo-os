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
#include <libspooky/data.h>
#include <libspooky/proxy.h>
#include <libspooky/structure.private.h>

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

SPOOKY_TYPE_DEF(sys_channel_t*, channel);

ferr_t spooky_retain_object_with_type(const void* object, spooky_type_t* type) {
	if (type == spooky_type_data()) {
		spooky_data_t* data = *(spooky_data_t* const*)object;

		if (data) {
			return spooky_retain(data);
		}
	} else if (type == spooky_type_proxy()) {
		spooky_proxy_t* proxy = *(spooky_proxy_t* const*)object;

		if (proxy) {
			return spooky_retain(proxy);
		}
	} else if (spooky_object_class(type) == spooky_object_class_structure()) {
		spooky_structure_object_t* structure = (void*)type;

		for (size_t i = 0; i < structure->member_count; ++i) {
			ferr_t status = spooky_retain_object_with_type((const char*)object + structure->members[i].offset, structure->members[i].type);
			if (status != ferr_ok) {
				// release all members that we successfully retained
				for (size_t j = 0; j < i; ++j) {
					spooky_release_object_with_type((const char*)object + structure->members[j].offset, structure->members[j].type);
				}
				return status;
			}
		}
	} else if (type == spooky_type_channel()) {
		sys_channel_t* channel = *(sys_channel_t* const*)object;

		if (channel) {
			return sys_retain(channel);
		}
	}

	return ferr_ok;
};

void spooky_release_object_with_type(const void* object, spooky_type_t* type) {
	if (type == spooky_type_data()) {
		spooky_data_t* data = *(spooky_data_t* const*)object;

		if (data) {
			spooky_release(data);
		}
	} else if (type == spooky_type_proxy()) {
		spooky_proxy_t* proxy = *(spooky_proxy_t* const*)object;

		if (proxy) {
			spooky_release(proxy);
		}
	} else if (spooky_object_class(type) == spooky_object_class_structure()) {
		spooky_structure_object_t* structure = (void*)type;

		for (size_t i = 0; i < structure->member_count; ++i) {
			spooky_release_object_with_type((const char*)object + structure->members[i].offset, structure->members[i].type);
		}
	} else if (type == spooky_type_channel()) {
		sys_channel_t* channel = *(sys_channel_t* const*)object;

		if (channel) {
			sys_release(channel);
		}
	}
};
