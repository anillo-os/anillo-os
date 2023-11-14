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

#include <libeve/server-channel.private.h>

//
// unsupported APIs
//

ferr_t eve_server_channel_create(sys_channel_t* sys_server_channel, void* context, eve_server_channel_t** out_server_channel) {
	return ferr_unsupported;
};

void eve_server_channel_set_handler(eve_server_channel_t* server_channel, eve_server_channel_handler_f handler) {
	// no-op
};

void eve_server_channel_set_peer_close_handler(eve_server_channel_t* server_channel, eve_server_channel_close_handler_f handler) {
	// no-op
};

ferr_t eve_server_channel_target(eve_server_channel_t* server_channel, bool retain, sys_channel_t** out_sys_server_channel) {
	return ferr_unsupported;
};
