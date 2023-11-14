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

#ifndef _LIBEVE_SERVER_CHANNEL_PRIVATE_H_
#define _LIBEVE_SERVER_CHANNEL_PRIVATE_H_

#include <libeve/server-channel.h>
#include <libeve/objects.private.h>

LIBEVE_DECLARATIONS_BEGIN;

LIBEVE_STRUCT(eve_server_channel_object) {
	sys_object_t object;
	sys_channel_t* sysman_server_channel;
	sys_monitor_item_t* monitor_item;
	void* context;
	eve_item_destructor_f destructor;
	eve_server_channel_handler_f handler;
	eve_server_channel_close_handler_f close_handler;
};

LIBEVE_DECLARATIONS_END;

#endif // _LIBEVE_SERVER_CHANNEL_PRIVATE_H_
