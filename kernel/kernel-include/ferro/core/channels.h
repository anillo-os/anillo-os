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

#ifndef _FERRO_CORE_CHANNELS_H_
#define _FERRO_CORE_CHANNELS_H_

#include <ferro/base.h>
#include <ferro/error.h>

#include <ferro/core/waitq.h>
#include <ferro/core/locks.h>

#include <ferro/api.h>

FERRO_DECLARATIONS_BEGIN;

FERRO_STRUCT_FWD(fchannel);
FERRO_STRUCT_FWD(fpage_mapping);

FERRO_ENUM(uint64_t, fchannel_message_attachment_data_flags) {
	fchannel_message_attachment_data_flag_shared = 1 << 0,
};

FERRO_STRUCT(fchannel_message_attachment_header) {
	uint64_t next_offset;
	uint64_t length;
	fchannel_message_attachment_type_t type;
};

FERRO_STRUCT(fchannel_message_attachment_channel) {
	fchannel_message_attachment_header_t header;
	fchannel_t* channel;
};

FERRO_STRUCT(fchannel_message_attachment_null) {
	fchannel_message_attachment_header_t header;
};

FERRO_STRUCT(fchannel_message_attachment_mapping) {
	fchannel_message_attachment_header_t header;
	fpage_mapping_t* mapping;
};

FERRO_STRUCT(fchannel_message_attachment_data) {
	fchannel_message_attachment_header_t header;
	fchannel_message_attachment_data_flags_t flags;
	uint64_t length;
	union {
		fpage_mapping_t* shared_data;
		void* copied_data;
	};
};

FERRO_STRUCT(fchannel_message) {
	fchannel_conversation_id_t conversation_id;
	fchannel_message_id_t message_id;

	void* body;
	uint64_t body_length;

	fchannel_message_attachment_header_t* attachments;
	uint64_t attachments_length;
};

FERRO_STRUCT(fchannel) {
	fwaitq_t message_arrival_waitq;
	fwaitq_t queue_empty_waitq;
	fwaitq_t queue_removal_waitq;
	fwaitq_t close_waitq;
	fwaitq_t queue_full_waitq;
};

FERRO_STRUCT(fchannel_server) {
	fwaitq_t client_arrival_waitq;
	fwaitq_t queue_empty_waitq;
	fwaitq_t close_waitq;
};

FERRO_STRUCT_FWD(fchannel_realm);

FERRO_ENUM(uint64_t, fchannel_connect_flags) {
	fchannel_connect_flag_no_wait = 1 << 0,
	fchannel_connect_flag_interruptible = 1 << 1,
};

FERRO_ENUM(uint64_t, fchannel_receive_flags) {
	fchannel_receive_flag_no_wait = 1 << 0,
	fchannel_receive_flag_interruptible = 1 << 1,
};

FERRO_ENUM(uint64_t, fchannel_send_kernel_flags) {
	fchannel_send_kernel_flag_interruptible = 1ull << 32,
};

FERRO_ENUM(uint64_t, fchannel_server_accept_kernel_flags) {
	fchannel_server_accept_kernel_flag_interruptible = 1ull << 32,
};

void fchannel_init(void);

FERRO_WUR ferr_t fchannel_retain(fchannel_t* channel);
void fchannel_release(fchannel_t* channel);

FERRO_WUR ferr_t fchannel_realm_retain(fchannel_realm_t* realm);
void fchannel_realm_release(fchannel_realm_t* realm);

FERRO_WUR ferr_t fchannel_server_retain(fchannel_server_t* server);
void fchannel_server_release(fchannel_server_t* server);

FERRO_WUR ferr_t fchannel_realm_new(fchannel_realm_t* parent, fchannel_realm_t** out_realm);

FERRO_WUR ferr_t fchannel_realm_lookup(fchannel_realm_t* realm, const char* name, size_t name_length, fchannel_server_t** out_server);
FERRO_WUR ferr_t fchannel_realm_publish(fchannel_realm_t* realm, const char* name, size_t name_length, fchannel_server_t* server);
FERRO_WUR ferr_t fchannel_realm_unpublish(fchannel_realm_t* realm, const char* name, size_t name_length);

FERRO_WUR ferr_t fchannel_new_pair(fchannel_t** out_channel_1, fchannel_t** out_channel_2);
FERRO_WUR ferr_t fchannel_connect(fchannel_server_t* server, fchannel_connect_flags_t flags, fchannel_t** out_channel);

fchannel_t* fchannel_peer(fchannel_t* channel, bool retain);

fchannel_conversation_id_t fchannel_next_conversation_id(fchannel_t* channel);

FERRO_WUR ferr_t fchannel_send(fchannel_t* channel, fchannel_send_flags_t flags, fchannel_message_t* in_out_message);
FERRO_WUR ferr_t fchannel_receive(fchannel_t* channel, fchannel_receive_flags_t flags, fchannel_message_t* out_message);
FERRO_WUR ferr_t fchannel_close(fchannel_t* channel);

FERRO_WUR ferr_t fchannel_server_new(fchannel_server_t** out_server);
FERRO_WUR ferr_t fchannel_server_accept(fchannel_server_t* server, fchannel_server_accept_flags_t flags, fchannel_t** out_channel);
FERRO_WUR ferr_t fchannel_server_close(fchannel_server_t* server);

void fchannel_message_destroy(fchannel_message_t* message);

fchannel_realm_t* fchannel_realm_global(void);

FERRO_DECLARATIONS_END;

#endif // _FERRO_CORE_CHANNELS_H_
