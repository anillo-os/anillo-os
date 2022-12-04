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

#ifndef _VFSMAN_OBJECTS_H_
#define _VFSMAN_OBJECTS_H_

#include <libsys/libsys.h>
#include <libspooky/base.h>

LIBSPOOKY_DECLARATIONS_BEGIN;

typedef sys_object_t vfsman_object_t;
typedef sys_object_class_t vfsman_object_class_t;

LIBSYS_WUR ferr_t vfsman_retain(vfsman_object_t* object);
void vfsman_release(vfsman_object_t* object);

#define VFSMAN_OBJECT_CLASS(_name) \
	typedef vfsman_object_t vfsman_ ## _name ## _t; \
	const vfsman_object_class_t* vfsman_object_class_ ## _name (void);

const vfsman_object_class_t* vfsman_object_class(vfsman_object_t* object);

LIBSPOOKY_DECLARATIONS_END;

#endif // _VFSMAN_OBJECTS_H_
