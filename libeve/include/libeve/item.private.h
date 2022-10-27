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

#ifndef _LIBEVE_ITEM_PRIVATE_H_
#define _LIBEVE_ITEM_PRIVATE_H_

#include <libeve/item.h>
#include <libeve/objects.private.h>

LIBEVE_DECLARATIONS_BEGIN;

typedef void (*eve_item_handle_events_f)(eve_item_t* self, sys_monitor_events_t events);
typedef sys_monitor_item_t* (*eve_item_get_monitor_item_f)(eve_item_t* self);
typedef void (*eve_item_poll_after_attach_f)(eve_item_t* self);
typedef void (*eve_item_set_destructor_f)(eve_item_t* self, eve_item_destructor_f destructor);
typedef void* (*eve_item_get_context_f)(eve_item_t* self);

LIBEVE_STRUCT(eve_item_interface) {
	sys_object_interface_t interface;
	eve_item_handle_events_f handle_events;
	eve_item_get_monitor_item_f get_monitor_item;
	eve_item_poll_after_attach_f poll_after_attach;
	eve_item_set_destructor_f set_destructor;
	eve_item_get_context_f get_context;
};

#define LIBEVE_ITEM_INTERFACE(_next) \
	.interface = { \
		.namespace = sys_object_interface_namespace_libeve, \
		.type = eve_object_interface_type_item, \
		.next = (_next), \
	}

LIBEVE_DECLARATIONS_END;

#endif // _LIBEVE_ITEM_PRIVATE_H_
