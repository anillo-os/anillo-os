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

#ifndef _LIBSYS_CHANNELS_H_
#define _LIBSYS_CHANNELS_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include <libsys/base.h>
#include <libsys/objects.h>

LIBSYS_DECLARATIONS_BEGIN;

LIBSYS_OBJECT_CLASS(channel);
LIBSYS_OBJECT_CLASS(channel_message);
LIBSYS_OBJECT_CLASS(shared_memory);
LIBSYS_OBJECT_CLASS(data);

LIBSYS_ENUM(uint64_t, sys_channel_conversation_id) {
	sys_channel_conversation_id_none = 0,
};

LIBSYS_ENUM(uint64_t, sys_channel_send_flags) {
	sys_channel_send_flag_no_wait            = 1 << 0,
	sys_channel_send_flag_start_conversation = 1 << 1,
};

LIBSYS_ENUM(uint64_t, sys_channel_receive_flags) {
	sys_channel_receive_flag_no_wait = 1 << 0,
};

LIBSYS_ENUM(uint64_t, sys_channel_message_attachment_type) {
	sys_channel_message_attachment_type_invalid = 0,
	sys_channel_message_attachment_type_channel,
	sys_channel_message_attachment_type_shared_memory,
	sys_channel_message_attachment_type_data,
};

LIBSYS_ENUM(uint64_t, sys_channel_message_attachment_index) {
	sys_channel_message_attachment_index_invalid = UINT64_MAX,
};

LIBSYS_TYPED_FUNC(void, sys_channel_connect_async_callback, void* context, sys_channel_t* channel);

LIBSYS_WUR ferr_t sys_channel_create_pair(sys_channel_t** out_first, sys_channel_t** out_second);

LIBSYS_WUR ferr_t sys_channel_connect_sync(const char* server_name, sys_channel_t** out_channel);
LIBSYS_WUR ferr_t sys_channel_connect_sync_n(const char* server_name, size_t server_name_length, sys_channel_t** out_channel);

LIBSYS_WUR ferr_t sys_channel_connect_async(const char* server_name, sys_channel_connect_async_callback_f callback, void* context);
LIBSYS_WUR ferr_t sys_channel_connect_async_n(const char* server_name, size_t server_name_length, sys_channel_connect_async_callback_f callback, void* context);

LIBSYS_WUR ferr_t sys_channel_conversation_create(sys_channel_t* channel, sys_channel_conversation_id_t* out_conversation_id);

/**
 * Sends the given message on the given channel.
 *
 * Sending a message consumes it; this is because certain attachments that can be sent along with the message are one-time-use-only.
 * Therefore, the caller must be holding the only reference to the message when it is sent. This function does not verify this condition,
 * however, as doing so would be inherently racy.
 *
 * Upon success, this operation will consume the caller's reference on the message. Upon failure, the caller will still have their
 * reference on the message; i.e. in this case, the operation will not modify the message or its reference count in any way.
 *
 * In fact, sending is atomic: either the message is sent or it is not; it cannot be partially sent or consumed.
 * Upon failure, the message and all of its attachments and related data will remain as if the operation had not even been attempted.
 */
LIBSYS_WUR ferr_t sys_channel_send(sys_channel_t* channel, sys_channel_send_flags_t flags, sys_channel_message_t* message, sys_channel_conversation_id_t* out_conversation_id);

LIBSYS_WUR ferr_t sys_channel_receive(sys_channel_t* channel, sys_channel_receive_flags_t flags, sys_channel_message_t** out_message);

/**
 * Closes this end of the channel immediately.
 *
 * Closing a channel end actually means it will not send any more messages. However, it can still receive messages from the other end of the channel.
 *
 * This operation will abort all pending sends with ferr_permanent_outage and prevent future sends (returning ferr_permanent_outage on such attempts).
 *
 * This is NOT recommended for normal operation. The channel will be closed automatically when the last reference to it is released;
 * that should be the preferred way of closing a channel. This is only meant for special cases (e.g. when you encounter some error
 * and need to abort sends and indicate this to your peer).
 *
 * This operation is also useful if the channel is being monitored in a monitor item. In that case, the monitor item will retain a reference
 * on the channel which prevents it from being automatically closed. NOTE: This behavior may change in the future.
 *
 * This operation does NOT invalidate any references to the channel nor does it prevent it from being retained or released.
 */
void sys_channel_close(sys_channel_t* channel);

LIBSYS_WUR ferr_t sys_channel_message_create(size_t initial_length, sys_channel_message_t** out_message);
LIBSYS_WUR ferr_t sys_channel_message_create_copy(const void* data, size_t length, sys_channel_message_t** out_message);
LIBSYS_WUR ferr_t sys_channel_message_create_nocopy(void* data, size_t length, sys_channel_message_t** out_message);

size_t sys_channel_message_length(sys_channel_message_t* message);
void* sys_channel_message_data(sys_channel_message_t* message);

LIBSYS_WUR ferr_t sys_channel_message_extend(sys_channel_message_t* message, size_t extra_length);

/**
 * Appends the given channel as an attachment on the given message.
 *
 * Attaching a channel to a message transfers ownership of the channel into the message.
 *
 * Only channels which the caller fully owns may be transferred. In other words,
 * the caller must be holding the only reference on the channel in order to attach it to the message.
 * This function does not verify this condition, however, as doing so would be inherently racy.
 *
 * Upon success, this operation will consume the caller's reference on the channel. Upon failure,
 * the caller will still have their reference on the channel; i.e. in this case, the operation will not
 * modify the channel object or its reference count in any way.
 */
LIBSYS_WUR ferr_t sys_channel_message_attach_channel(sys_channel_message_t* message, sys_channel_t* channel, sys_channel_message_attachment_index_t* out_attachment_index);

LIBSYS_WUR ferr_t sys_channel_message_attach_shared_memory(sys_channel_message_t* message, sys_shared_memory_t* shared_memory, sys_channel_message_attachment_index_t* out_attachment_index);

LIBSYS_WUR ferr_t sys_channel_message_attach_data(sys_channel_message_t* message, sys_data_t* data, bool copy, sys_channel_message_attachment_index_t* out_attachment_index);

size_t sys_channel_message_attachment_count(sys_channel_message_t* message);

sys_channel_message_attachment_type_t sys_channel_message_attachment_type(sys_channel_message_t* message, sys_channel_message_attachment_index_t attachment_index);

/**
 * Detaches the channel attached to the given message at the given index and returns it.
 *
 * Detaching a channel from a message transfers ownership of the channel to the caller.
 */
LIBSYS_WUR ferr_t sys_channel_message_detach_channel(sys_channel_message_t* message, sys_channel_message_attachment_index_t attachment_index, sys_channel_t** out_channel);

LIBSYS_WUR ferr_t sys_channel_message_detach_shared_memory(sys_channel_message_t* message, sys_channel_message_attachment_index_t attachment_index, sys_shared_memory_t** out_shared_memory);

LIBSYS_WUR ferr_t sys_channel_message_detach_data(sys_channel_message_t* message, sys_channel_message_attachment_index_t attachment_index, sys_data_t** out_data);

sys_channel_conversation_id_t sys_channel_message_get_conversation_id(sys_channel_message_t* message);
void sys_channel_message_set_conversation_id(sys_channel_message_t* message, sys_channel_conversation_id_t conversation_id);

LIBSYS_DECLARATIONS_END;

#endif // _LIBSYS_CHANNELS_H_
