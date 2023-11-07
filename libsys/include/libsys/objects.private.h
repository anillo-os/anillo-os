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

#ifndef _LIBSYS_OBJECTS_PRIVATE_H_
#define _LIBSYS_OBJECTS_PRIVATE_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include <libsys/objects.h>

LIBSYS_DECLARATIONS_BEGIN;

typedef ferr_t (*sys_object_retain_f)(sys_object_t* object);
typedef void (*sys_object_release_f)(sys_object_t* object);
typedef void (*sys_object_destroy_f)(sys_object_t* object);

LIBSYS_ENUM(uint32_t, sys_object_interface_namespace) {
	sys_object_interface_namespace_libsys = 0,
};

LIBSYS_ENUM(uint32_t, sys_object_interface_type) {
	sys_object_interface_type_class = 0,
};

LIBSYS_STRUCT(sys_object_interface) {
	sys_object_interface_namespace_t namespace;
	sys_object_interface_type_t type;
	const sys_object_interface_t* next;
};

LIBSYS_STRUCT(sys_object_class) {
	sys_object_interface_t interface;
	sys_object_retain_f retain;
	sys_object_release_f release;
	sys_object_destroy_f destroy;
};

LIBSYS_OPTIONS(uint64_t, sys_object_flags) {
	sys_object_flag_free_on_destroy = 1ULL << 0,
	sys_object_flag_immortal        = 1ULL << 1,
};

LIBSYS_STRUCT(sys_object) {
	const sys_object_class_t* object_class;
	uint64_t reference_count;
	sys_object_flags_t flags;
};

ferr_t sys_object_init(sys_object_t* object, const sys_object_class_t* object_class);
void sys_object_destroy(sys_object_t* object);
ferr_t sys_object_retain(sys_object_t* object);
void sys_object_release(sys_object_t* object);

// for global/static objects
ferr_t sys_object_retain_noop(sys_object_t* object);
void sys_object_release_noop(sys_object_t* object);

ferr_t sys_object_new(const sys_object_class_t* object_class, size_t extra_bytes, sys_object_t** out_object);

#define LIBSYS_OBJECT_CLASS_GETTER(_name, _class_variable) const sys_object_class_t* sys_object_class_ ## _name (void) { return &_class_variable; };

LIBSYS_ALWAYS_INLINE
const sys_object_interface_t* sys_object_interface_find(const sys_object_interface_t* interface, sys_object_interface_namespace_t namespace, sys_object_interface_type_t type) {
	for (; interface != NULL; interface = interface->next) {
		if (interface->namespace == namespace && interface->type == type) {
			return interface;
		}
	}
	return NULL;
};

#define LIBSYS_OBJECT_CLASS_INTERFACE(_next) \
	.interface = { \
		.namespace = sys_object_interface_namespace_libsys, \
		.type = sys_object_interface_type_class, \
		.next = (_next), \
	}

LIBSYS_DECLARATIONS_END;

#endif // _LIBSYS_OBJECTS_PRIVATE_H_
