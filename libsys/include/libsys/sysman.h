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

#ifndef _LIBSYS_SYSMAN_H_
#define _LIBSYS_SYSMAN_H_

#include <stdint.h>

#include <libsys/base.h>
#include <libsys/channels.h>

LIBSYS_DECLARATIONS_BEGIN;

LIBSYS_ENUM(uint8_t, sys_sysman_realm) {
	sys_sysman_realm_invalid = 0,
	sys_sysman_realm_global = 1,
	sys_sysman_realm_local = 2,
	sys_sysman_realm_children = 3,
};

LIBSYS_TYPED_FUNC(void, sys_sysman_register_callback, void* context, sys_channel_t* server_channel);

LIBSYS_WUR ferr_t sys_sysman_register_sync(const char* name, sys_sysman_realm_t realm, sys_channel_t** out_server_channel);
LIBSYS_WUR ferr_t sys_sysman_register_sync_n(const char* name, size_t name_length, sys_sysman_realm_t realm, sys_channel_t** out_server_channel);

LIBSYS_WUR ferr_t sys_sysman_register_async(const char* name, sys_sysman_realm_t realm, sys_sysman_register_callback_f callback, void* context);
LIBSYS_WUR ferr_t sys_sysman_register_async_n(const char* name, size_t name_length, sys_sysman_realm_t realm, sys_sysman_register_callback_f callback, void* context);

LIBSYS_DECLARATIONS_END;

#endif // _LIBSYS_SYSMAN_H_
