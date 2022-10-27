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

#ifndef _NETMAN_OBJECTS_H_
#define _NETMAN_OBJECTS_H_

#include <libsys/libsys.h>
#include <netman/base.h>

NETMAN_DECLARATIONS_BEGIN;

typedef sys_object_t netman_object_t;
typedef sys_object_class_t netman_object_class_t;

LIBSYS_WUR ferr_t netman_retain(netman_object_t* object);
void netman_release(netman_object_t* object);

#define NETMAN_OBJECT_CLASS(_name) \
	typedef netman_object_t netman_ ## _name ## _t; \
	const netman_object_class_t* netman_object_class_ ## _name (void);

const netman_object_class_t* netman_object_class(netman_object_t* object);

NETMAN_DECLARATIONS_END;

#endif // _NETMAN_OBJECTS_H_
