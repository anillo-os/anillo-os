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

#ifndef _LIBSPOOKY_INVOCATION_PRIVATE_H_
#define _LIBSPOOKY_INVOCATION_PRIVATE_H_

#include <libspooky/invocation.h>
#include <libspooky/objects.private.h>

LIBSPOOKY_DECLARATIONS_BEGIN;

LIBSPOOKY_STRUCT(spooky_invocation_outgoing_callback_info) {
	sys_channel_conversation_id_t conversation_id;
	eve_channel_cancellation_token_t cancellation_token;
	size_t index;
	spooky_function_implementation_f implementation;
	void* context;
	// TODO: maybe add a mutex here? or maybe add one to the invocation object as a whole?
};

LIBSPOOKY_STRUCT(spooky_invocation_incoming_callback_info) {
	sys_channel_conversation_id_t conversation_id;
	size_t index;
};

LIBSPOOKY_STRUCT(spooky_invocation_object) {
	spooky_object_t object;
	spooky_function_t* function_type;
	eve_channel_t* channel;
	bool incoming;
	sys_channel_conversation_id_t conversation_id;
	size_t outgoing_callback_info_count;
	spooky_invocation_outgoing_callback_info_t* outgoing_callback_infos;
	size_t incoming_callback_info_count;
	spooky_invocation_incoming_callback_info_t* incoming_callback_infos;
	char* incoming_data;
	char* outgoing_data;
	char* name;
	size_t name_length;
	spooky_proxy_t* proxy;
};

ferr_t spooky_invocation_create_incoming(eve_channel_t* channel, sys_channel_message_t* message, spooky_invocation_t** out_invocation);

LIBSPOOKY_DECLARATIONS_END;

#endif // _LIBSPOOKY_INVOCATION_PRIVATE_H_
