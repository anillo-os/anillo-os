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

#ifndef _LIBEVE_OBJECTS_PRIVATE_H_
#define _LIBEVE_OBJECTS_PRIVATE_H_

#include <libeve/base.h>
#include <libeve/objects.h>
#include <libsys/objects.private.h>

LIBEVE_DECLARATIONS_BEGIN;

enum {
	sys_object_interface_namespace_libeve = 0xe4e,
};

LIBEVE_ENUM(uint32_t, eve_object_interface_type) {
	eve_object_interface_type_item = 0,
};

LIBEVE_DECLARATIONS_END;

#endif // _LIBEVE_OBJECTS_PRIVATE_H_
