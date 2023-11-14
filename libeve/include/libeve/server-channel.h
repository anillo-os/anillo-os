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

#ifndef _LIBEVE_SERVER_CHANNEL_H_
#define _LIBEVE_SERVER_CHANNEL_H_

#include <libeve/base.h>
#include <libeve/objects.h>

#include <libeve/loop.h>

LIBEVE_DECLARATIONS_BEGIN;

LIBEVE_ITEM_CLASS(server_channel);

LIBEVE_TYPED_FUNC(void, eve_server_channel_handler, void* context, eve_server_channel_t* server_channel, sys_channel_t* channel);
LIBEVE_TYPED_FUNC(void, eve_server_channel_close_handler, void* context, eve_server_channel_t* server_channel);

LIBEVE_WUR ferr_t eve_server_channel_create(sys_channel_t* sysman_server_channel, void* context, eve_server_channel_t** out_server_channel);
void eve_server_channel_set_handler(eve_server_channel_t* server_channel, eve_server_channel_handler_f handler);
void eve_server_channel_set_peer_close_handler(eve_server_channel_t* server_channel, eve_server_channel_close_handler_f handler);
LIBEVE_WUR ferr_t eve_server_channel_target(eve_server_channel_t* server_channel, bool retain, sys_channel_t** out_sysman_server_channel);

LIBEVE_DECLARATIONS_END;

#endif // _LIBEVE_SERVER_CHANNEL_H_
