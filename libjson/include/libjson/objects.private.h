/*
 * This file is part of Anillo OS
 * Copyright (C) 2023 Anillo OS Developers
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

#ifndef _LIBJSON_OBJECTS_PRIVATE_H_
#define _LIBJSON_OBJECTS_PRIVATE_H_

#include <libjson/base.h>
#include <libjson/objects.h>
#include <libsys/objects.private.h>

LIBJSON_DECLARATIONS_BEGIN;

enum {
	sys_object_interface_namespace_libjson = 0x7502,
};

LIBJSON_ENUM(uint32_t, json_object_interface_type) {
	json_object_interface_type_xxx_reserved = 0,
};

LIBJSON_DECLARATIONS_END;

#endif // _LIBJSON_OBJECTS_PRIVATE_H_
