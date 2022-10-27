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

#ifndef _LIBSPOOKY_INTERFACE_H_
#define _LIBSPOOKY_INTERFACE_H_

#include <libspooky/base.h>
#include <libspooky/function.h>
#include <libeve/libeve.h>

LIBSPOOKY_DECLARATIONS_BEGIN;

LIBSPOOKY_OBJECT_CLASS(interface);

LIBSPOOKY_STRUCT(spooky_interface_entry) {
	const char* name;
	size_t name_length;
	spooky_function_t* function;
	spooky_function_implementation_f implementation;
	void* context;
};

LIBSPOOKY_WUR ferr_t spooky_interface_create(const spooky_interface_entry_t* entries, size_t entry_count, spooky_interface_t** out_interface);
LIBSPOOKY_WUR ferr_t spooky_interface_adopt(spooky_interface_t* interface, sys_channel_t* channel, eve_loop_t* loop);
LIBSPOOKY_WUR ferr_t spooky_interface_handle(spooky_interface_t* interface, sys_channel_message_t* message, eve_channel_t* channel);

LIBSPOOKY_DECLARATIONS_END;

#endif // _LIBSPOOKY_INTERFACE_H_
