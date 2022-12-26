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

#ifndef _LIBSPOOKY_PROXY_PRIVATE_H_
#define _LIBSPOOKY_PROXY_PRIVATE_H_

#include <libspooky/proxy.h>
#include <libspooky/objects.private.h>

LIBSPOOKY_DECLARATIONS_BEGIN;

LIBSPOOKY_STRUCT(spooky_proxy_interface_object) {
	spooky_object_t object;
	spooky_proxy_interface_entry_t* entries;
	size_t entry_count;
};

LIBSPOOKY_STRUCT(spooky_proxy_object) {
	spooky_object_t object;
};

LIBSPOOKY_STRUCT(spooky_incoming_proxy_object) {
	spooky_proxy_object_t base;
	eve_channel_t* channel;
	eve_loop_t* loop;
};

LIBSPOOKY_STRUCT(spooky_outgoing_proxy_object) {
	spooky_proxy_object_t base;
	void* context;
	spooky_proxy_destructor_f destructor;
	spooky_proxy_interface_object_t* interface;
};

LIBSPOOKY_WUR ferr_t spooky_proxy_create_incoming(sys_channel_t* sys_channel, eve_loop_t* loop, spooky_proxy_t** out_proxy);
LIBSPOOKY_WUR ferr_t spooky_outgoing_proxy_create_channel(spooky_proxy_t* outgoing_proxy, sys_channel_t** out_sys_channel);

LIBSPOOKY_DECLARATIONS_END;

#endif // _LIBSPOOKY_PROXY_PRIVATE_H_
