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

#ifndef _LIBEVE_CHANNEL_PRIVATE_H_
#define _LIBEVE_CHANNEL_PRIVATE_H_

#include <libeve/channel.h>
#include <libeve/objects.private.h>
#include <libsimple/ring.h>

LIBEVE_DECLARATIONS_BEGIN;

LIBEVE_STRUCT(eve_channel_outbox_entry) {
	sys_channel_message_t* message;
	bool wants_reply;
	bool is_sync;
	struct {
		sys_semaphore_t* semaphore;
		ferr_t* out_error;
	} sync;
};

LIBEVE_STRUCT(eve_channel_outstanding_reply) {
	bool is_sync;
	eve_channel_cancellation_token_t cancellation_token;
	union {
		struct {
			eve_channel_reply_handler_f reply_handler;
			void* context;
		} async;
		struct {
			sys_semaphore_t* semaphore;
			sys_channel_message_t** out_message;
			ferr_t* out_error;
		} sync;
	};
};

LIBEVE_STRUCT(eve_channel_object) {
	sys_object_t object;
	sys_channel_t* sys_channel;
	sys_monitor_item_t* monitor_item;
	void* context;
	eve_item_destructor_f destructor;
	eve_channel_message_handler_f message_handler;
	eve_channel_peer_close_handler_f peer_close_handler;
	eve_channel_message_send_error_handler_f message_send_error_handler;
	bool can_send;
	bool inited_outbox;
	bool inited_oustanding_replies;
	simple_ring_t outbox;
	eve_channel_outbox_entry_t outbox_buffer[32];
	sys_mutex_t outbox_mutex;
	sys_mutex_t outstanding_replies_mutex;
	simple_ghmap_t outstanding_replies_table;
	eve_channel_cancellation_token_t next_cancellation_token;
};

LIBEVE_DECLARATIONS_END;

#endif // _LIBEVE_CHANNEL_PRIVATE_H_
