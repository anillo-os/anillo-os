/*
 * This file is part of Anillo OS
 * Copyright (C) 2025 Anillo OS Developers
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

#ifndef _SYSMAN_OBJECTS_PRIVATE_H_
#define _SYSMAN_OBJECTS_PRIVATE_H_

#include <sysman/base.h>
#include <sysman/objects.h>
#include <libsys/objects.private.h>

SYSMAN_DECLARATIONS_BEGIN;

enum {
	sys_object_interface_namespace_sysman = 0x5159a9,
};

SYSMAN_ENUM(uint32_t, sysman_object_interface_type) {
	sysman_object_interface_type_xxx_reserved = 0,
};

SYSMAN_WUR ferr_t sysman_object_new(const sysman_object_class_t* object_class, size_t extra_bytes, sysman_object_t** out_object);

SYSMAN_DECLARATIONS_END;

#endif // _SYSMAN_OBJECTS_PRIVATE_H_
