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

#ifndef _LIBSYS_OBJECTS_H_
#define _LIBSYS_OBJECTS_H_

#include <libsys/base.h>
#include <ferro/error.h>

LIBSYS_DECLARATIONS_BEGIN;

LIBSYS_STRUCT_FWD(sys_object);
LIBSYS_STRUCT_FWD(sys_object_class);

LIBSYS_WUR ferr_t sys_retain(sys_object_t* object);
void sys_release(sys_object_t* object);

#define LIBSYS_OBJECT_CLASS(_name) \
	typedef sys_object_t sys_ ## _name ## _t; \
	const sys_object_class_t* sys_object_class_ ## _name (void);

const sys_object_class_t* sys_object_class(sys_object_t* object);

LIBSYS_DECLARATIONS_END;

#endif // _LIBSYS_OBJECTS_H_
