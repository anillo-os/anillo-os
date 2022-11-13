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

#ifndef _LIBSPOOKY_PROXY_H_
#define _LIBSPOOKY_PROXY_H_

#include <libspooky/base.h>
#include <libspooky/function.h>
#include <libeve/libeve.h>

LIBSPOOKY_DECLARATIONS_BEGIN;

LIBSPOOKY_OBJECT_CLASS(proxy_interface);
LIBSPOOKY_OBJECT_CLASS(proxy);

LIBSPOOKY_STRUCT(spooky_proxy_interface_entry) {
	const char* name;
	size_t name_length;
	spooky_function_t* function;
	spooky_function_implementation_f implementation;
};

typedef void (*spooky_proxy_destructor_f)(void* context);

LIBSPOOKY_WUR ferr_t spooky_proxy_interface_create(const spooky_proxy_interface_entry_t* entries, size_t entry_count, spooky_proxy_interface_t** out_proxy_interface);

//
// general proxy functions
// (valid regardless of whether this proxy is incoming or outgoing)
//

/**
 * Incoming proxies are those that are created by our peer and received locally.
 * Outgoing proxies are those created locally and sent to our peer.
 */
LIBSPOOKY_WUR bool spooky_proxy_is_incoming(spooky_proxy_t* proxy);

//
// outgoing proxy functions
// (i.e. the side providing the proxy)
//

LIBSPOOKY_WUR ferr_t spooky_proxy_create(spooky_proxy_interface_t* interface, void* context, spooky_proxy_destructor_f destructor, spooky_proxy_t** out_proxy);
void* spooky_proxy_context(spooky_proxy_t* proxy);

//
// incoming proxy functions
// (i.e. the side receiving the proxy)
//

// nothing for now

LIBSPOOKY_DECLARATIONS_END;

#endif // _LIBSPOOKY_PROXY_H_
