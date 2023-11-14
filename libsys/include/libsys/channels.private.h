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

#ifndef _LIBSYS_CHANNELS_PRIVATE_H_
#define _LIBSYS_CHANNELS_PRIVATE_H_

#include <libsys/channels.h>
#include <libsys/objects.private.h>
#include <gen/libsyscall/syscall-wrappers.h>

LIBSYS_DECLARATIONS_BEGIN;

#define SYS_CHANNEL_DID_INVALID UINT64_MAX

LIBSYS_STRUCT(sys_channel_object) {
	sys_object_t object;
	uint64_t channel_did;
};

LIBSYS_STRUCT(sys_channel_message_attachment_channel_object) {
	sys_object_t object;
	uint64_t channel_did;
};

LIBSYS_STRUCT(sys_channel_message_object) {
	sys_object_t object;
	void* body;
	size_t body_length;
	sys_object_t** attachments;
	size_t attachment_count;
	sys_channel_conversation_id_t conversation_id;
	bool owns_body_buffer;
};

extern const sys_object_class_t __sys_object_class_channel;

FERRO_WUR ferr_t sys_channel_message_serialize(sys_channel_message_t* message, libsyscall_channel_message_t* out_syscall_message);
void sys_channel_message_consumed(sys_channel_message_t* message, libsyscall_channel_message_t* in_syscall_message);

LIBSYS_STRUCT(sys_channel_message_deserialization_context) {
	libsyscall_channel_message_t syscall_message;
	sys_channel_message_t* message;
};

FERRO_WUR ferr_t sys_channel_message_deserialize_begin(sys_channel_message_deserialization_context_t* out_context);
FERRO_WUR ferr_t sys_channel_message_deserialize_resize(sys_channel_message_deserialization_context_t* in_out_context);
FERRO_WUR ferr_t sys_channel_message_deserialize_prepare(sys_channel_message_deserialization_context_t* in_out_context);
void sys_channel_message_deserialize_abort(sys_channel_message_deserialization_context_t* in_context);
void sys_channel_message_deserialize_finalize(sys_channel_message_deserialization_context_t* in_context, sys_channel_message_t** out_message);

// these should be multiples of 2
#define SYS_CHANNEL_DEFAULT_SYSCALL_BODY_BUFFER_SIZE 512
// each attachment has a 24-byte header (with two 8-byte fields and one 1-byte field, padded to 8 bytes);
// let's round that up to 32 bytes per attachment. so with this default size, we can fit up to 2 attachments,
// on average (or 1 large attachment). message tend not to have too many attachments, so this is a good default:
// small enough not to be an issue usually, but large enough to handle the most common cases.
#define SYS_CHANNEL_DEFAULT_SYSCALL_ATTACHMENT_BUFFER_SIZE 64

// these can probably remain as 0 forever
#define SYS_CHANNEL_MINIMUM_SYSCALL_BODY_BUFFER_SIZE 0
#define SYS_CHANNEL_MINIMUM_SYSCALL_ATTACHMENT_BUFFER_SIZE 0

LIBSYS_DECLARATIONS_END;

#endif // _LIBSYS_CHANNELS_PRIVATE_H_
