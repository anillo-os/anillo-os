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

#include <libsys/channels.private.h>
#include <libsimple/libsimple.h>
#include <libsys/mempool.h>
#include <gen/libsyscall/syscall-wrappers.h>
#include <libsys/abort.h>
#include <libsys/pages.private.h>

static void channel_destroy(sys_object_t* object) {
	sys_channel_object_t* channel = (void*)object;

	if (channel->channel_did != SYS_CHANNEL_DID_INVALID) {
		libsyscall_wrapper_channel_close(channel->channel_did, true);
	}

	sys_object_destroy(object);
};

static void server_channel_destroy(sys_object_t* object) {
	sys_server_channel_object_t* server_channel = (void*)object;

	if (server_channel->server_did != SYS_CHANNEL_DID_INVALID) {
		libsyscall_wrapper_server_channel_close(server_channel->server_did, true);
	}

	sys_object_destroy(object);
};

static void channel_message_destroy(sys_object_t* object) {
	sys_channel_message_object_t* message = (void*)object;

	if (message->body && message->owns_body_buffer) {
		LIBSYS_WUR_IGNORE(sys_mempool_free(message->body));
	}

	for (size_t i = 0; i < message->attachment_count; ++i) {
		if (message->attachments[i]) {
			sys_release(message->attachments[i]);
		}
	}

	if (message->attachments) {
		LIBSYS_WUR_IGNORE(sys_mempool_free(message->attachments));
	}

	sys_object_destroy(object);
};

static void channel_message_attachment_channel_destroy(sys_object_t* object) {
	sys_channel_message_attachment_channel_object_t* attachment = (void*)object;

	if (attachment->channel_did != SYS_CHANNEL_DID_INVALID) {
		libsyscall_wrapper_channel_close(attachment->channel_did, true);
	}

	sys_object_destroy(object);
};

const sys_object_class_t __sys_object_class_channel = {
	LIBSYS_OBJECT_CLASS_INTERFACE(NULL),
	.destroy = channel_destroy,
};

LIBSYS_OBJECT_CLASS_GETTER(channel, __sys_object_class_channel);

static const sys_object_class_t server_channel_object_class = {
	LIBSYS_OBJECT_CLASS_INTERFACE(NULL),
	.destroy = server_channel_destroy,
};

LIBSYS_OBJECT_CLASS_GETTER(server_channel, server_channel_object_class);

static const sys_object_class_t channel_message_object_class = {
	LIBSYS_OBJECT_CLASS_INTERFACE(NULL),
	.destroy = channel_message_destroy,
};

LIBSYS_OBJECT_CLASS_GETTER(channel_message, channel_message_object_class);

static const sys_object_class_t channel_message_attachment_channel_object_class = {
	LIBSYS_OBJECT_CLASS_INTERFACE(NULL),
	.destroy = channel_message_attachment_channel_destroy,
};

LIBSYS_OBJECT_CLASS_GETTER(channel_message_attachment_channel, channel_message_attachment_channel_object_class);

ferr_t sys_channel_create_pair(sys_channel_t** out_first, sys_channel_t** out_second) {
	ferr_t status = ferr_ok;
	sys_channel_object_t* first = NULL;
	sys_channel_object_t* second = NULL;
	uint64_t channel_dids[2];

	// create the objects first, because allocating memory is cheap in comparison to making syscalls

	status = sys_object_new(&__sys_object_class_channel, sizeof(sys_channel_object_t) - sizeof(sys_object_t), (void*)&first);
	if (status != ferr_ok) {
		goto out;
	}

	first->channel_did = SYS_CHANNEL_DID_INVALID;

	status = sys_object_new(&__sys_object_class_channel, sizeof(sys_channel_object_t) - sizeof(sys_object_t), (void*)&second);
	if (status != ferr_ok) {
		goto out;
	}

	second->channel_did = SYS_CHANNEL_DID_INVALID;

	// now try to create the pair with a syscall
	status = libsyscall_wrapper_channel_create_pair(channel_dids);
	if (status != ferr_ok) {
		goto out;
	}

	first->channel_did = channel_dids[0];
	second->channel_did = channel_dids[1];

out:
	if (status == ferr_ok) {
		*out_first = (void*)first;
		*out_second = (void*)second;
	} else {
		if (first) {
			sys_release((void*)first);
		}
		if (second) {
			sys_release((void*)second);
		}
	}

	return status;
};

ferr_t sys_channel_connect(const char* server_name, sys_channel_realm_t realm, sys_channel_connect_flags_t flags, sys_channel_t** out_channel) {
	return sys_channel_connect_n(server_name, simple_strlen(server_name), realm, flags, out_channel);
};

static ferr_t sys_channel_realm_to_libsyscall_realm(sys_channel_realm_t realm, libsyscall_channel_realm_t* out_libsyscall_realm) {
	switch (realm) {
		case sys_channel_realm_local:
			*out_libsyscall_realm = libsyscall_channel_realm_local;
			break;
		case sys_channel_realm_global:
			*out_libsyscall_realm = libsyscall_channel_realm_global;
			break;
		case sys_channel_realm_parent:
			*out_libsyscall_realm = libsyscall_channel_realm_parent;
			break;
		case sys_channel_realm_children:
			*out_libsyscall_realm = libsyscall_channel_realm_children;
			break;
		default:
			return ferr_invalid_argument;
	}

	return ferr_ok;
};

ferr_t sys_channel_connect_n(const char* server_name, size_t server_name_length, sys_channel_realm_t realm, sys_channel_connect_flags_t flags, sys_channel_t** out_channel) {
	ferr_t status = ferr_ok;
	sys_channel_object_t* channel = NULL;
	libsyscall_channel_realm_t libsyscall_realm;
	libsyscall_channel_connect_flags_t libsyscall_flags = 0;

	status = sys_channel_realm_to_libsyscall_realm(realm, &libsyscall_realm);
	if (status != ferr_ok) {
		goto out;
	}

	if ((flags & sys_channel_connect_flag_no_wait) != 0) {
		libsyscall_flags |= libsyscall_channel_connect_flag_no_wait;
	}

	if ((flags & sys_channel_connect_flag_recursive_realm) != 0) {
		libsyscall_flags |= libsyscall_channel_connect_flag_recursive_realm;
	}

	// create the object first, because allocating memory is cheap in comparison to making syscalls

	status = sys_object_new(&__sys_object_class_channel, sizeof(sys_channel_object_t) - sizeof(sys_object_t), (void*)&channel);
	if (status != ferr_ok) {
		goto out;
	}

	channel->channel_did = SYS_CHANNEL_DID_INVALID;

	// now try to make the syscall

	status = libsyscall_wrapper_channel_connect(server_name, server_name_length, libsyscall_realm, libsyscall_flags, &channel->channel_did);
	if (status != ferr_ok) {
		goto out;
	}

out:
	if (status == ferr_ok) {
		*out_channel = (void*)channel;
	} else {
		if (channel) {
			sys_release((void*)channel);
		}
	}
	return status;
};

ferr_t sys_channel_conversation_create(sys_channel_t* object, sys_channel_conversation_id_t* out_conversation_id) {
	sys_channel_object_t* channel = (void*)object;
	return libsyscall_wrapper_channel_conversation_create(channel->channel_did, out_conversation_id);
};

ferr_t sys_channel_send(sys_channel_t* object, sys_channel_send_flags_t flags, sys_channel_message_t* message, sys_channel_conversation_id_t* out_conversation_id) {
	ferr_t status = ferr_ok;
	sys_channel_object_t* channel = (void*)object;
	fchannel_send_flags_t libsyscall_flags = 0;
	libsyscall_channel_message_t libsyscall_message;

	if ((flags & sys_channel_send_flag_no_wait) != 0) {
		libsyscall_flags |= fchannel_send_flag_no_wait;
	}

	if ((flags & sys_channel_send_flag_start_conversation) != 0) {
		libsyscall_flags |= fchannel_send_flag_start_conversation;
	}

	status = sys_channel_message_serialize(message, &libsyscall_message);
	if (status != ferr_ok) {
		goto out;
	}

	status = libsyscall_wrapper_channel_send(channel->channel_did, libsyscall_flags, 0, libsyscall_timeout_type_none, &libsyscall_message);
	if (status != ferr_ok) {
		goto out;
	}

	sys_channel_message_consumed(message, &libsyscall_message);

	if (out_conversation_id) {
		*out_conversation_id = libsyscall_message.conversation_id;
	}

out:
	return status;
};

ferr_t sys_channel_receive(sys_channel_t* object, sys_channel_receive_flags_t flags, sys_channel_message_t** out_message) {
	ferr_t status = ferr_ok;
	sys_channel_object_t* channel = (void*)object;
	libsyscall_channel_receive_flags_t libsyscall_flags = 0;
	sys_channel_message_deserialization_context_t deserialization_context;
	bool abort_message_on_fail = false;

	if ((flags & sys_channel_receive_flag_no_wait) != 0) {
		libsyscall_flags |= libsyscall_channel_receive_flag_no_wait;
	}

	status = sys_channel_message_deserialize_begin(&deserialization_context);
	if (status != ferr_ok) {
		goto out;
	}

	abort_message_on_fail = true;

retry:
	// first, let's do a pre-receive peek to determine what resources we need to allocate
	status = libsyscall_wrapper_channel_receive(channel->channel_did, libsyscall_flags | libsyscall_channel_receive_flag_pre_receive_peek, 0, libsyscall_timeout_type_none, &deserialization_context.syscall_message);

	if (status != ferr_ok) {
		if (status == ferr_too_big) {
			// resize the buffers to match the size reported in the syscall message
			status = sys_channel_message_deserialize_resize(&deserialization_context);
			if (status != ferr_ok) {
				goto out;
			}

			// now try again
			goto retry;
		}

		// for all other errors, report them back to the caller
		goto out;
	}

	// okay, now let's try to allocate resources to receive the message
	status = sys_channel_message_deserialize_prepare(&deserialization_context);

	if (status != ferr_ok) {
		goto out;
	}

	// now let's try to receive the message
	status = libsyscall_wrapper_channel_receive(channel->channel_did, libsyscall_channel_receive_flag_no_wait | libsyscall_channel_receive_flag_match_message_id, 0, libsyscall_timeout_type_none, &deserialization_context.syscall_message);

	if (status != ferr_ok) {
		if (status == ferr_resource_unavailable) {
			// someone else grabbed the message we peeked before we had a chance to fully receive it.
			// let's go back and try to get another message.
			//
			// TODO: we could optimize this case by instructing the kernel to try peeking a message and returning that instead.

			// first, discard any resources we had allocated for the message we peeked.
			sys_channel_message_deserialize_abort(&deserialization_context);

			abort_message_on_fail = false;

			// now allocate some minimal resources to receive another message.
			status = sys_channel_message_deserialize_begin(&deserialization_context);
			if (status != ferr_ok) {
				goto out;
			}

			abort_message_on_fail = true;

			goto retry;
		}

		// for all other errors, report them back to the caller
		goto out;
	}

	// now finalize the message
	sys_channel_message_deserialize_finalize(&deserialization_context, out_message);

out:
	if (status != ferr_ok) {
		if (abort_message_on_fail) {
			sys_channel_message_deserialize_abort(&deserialization_context);
		}
	}

	return status;
};

void sys_channel_close(sys_channel_t* object) {
	sys_channel_object_t* channel = (void*)object;
	libsyscall_wrapper_channel_close(channel->channel_did, false);
};

ferr_t sys_server_channel_create(const char* name, sys_channel_realm_t realm, sys_server_channel_t** out_server_channel) {
	return sys_server_channel_create_n(name, simple_strlen(name), realm, out_server_channel);
};

ferr_t sys_server_channel_create_n(const char* name, size_t name_length, sys_channel_realm_t realm, sys_server_channel_t** out_server_channel) {
	ferr_t status = ferr_ok;
	libsyscall_channel_realm_t libsyscall_realm;
	sys_server_channel_object_t* server_channel = NULL;

	status = sys_channel_realm_to_libsyscall_realm(realm, &libsyscall_realm);
	if (status != ferr_ok) {
		goto out;
	}

	status = sys_object_new(&server_channel_object_class, sizeof(sys_server_channel_object_t) - sizeof(sys_object_t), (void*)&server_channel);
	if (status != ferr_ok) {
		goto out;
	}

	server_channel->server_did = SYS_CHANNEL_DID_INVALID;

	status = libsyscall_wrapper_server_channel_create(name, name_length, libsyscall_realm, &server_channel->server_did);
	if (status != ferr_ok) {
		goto out;
	}

out:
	if (status == ferr_ok) {
		*out_server_channel = (void*)server_channel;
	} else {
		if (server_channel) {
			sys_release((void*)server_channel);
		}
	}

	return status;
};

ferr_t sys_server_channel_accept(sys_server_channel_t* object, sys_server_channel_accept_flags_t flags, sys_channel_t** out_channel) {
	sys_server_channel_object_t* server_channel = (void*)object;
	ferr_t status = ferr_ok;
	sys_channel_object_t* channel = NULL;
	fchannel_server_accept_flags_t libsyscall_flags = 0;

	if ((flags & sys_server_channel_accept_flag_no_wait) != 0) {
		libsyscall_flags |= fserver_channel_accept_flag_no_wait;
	}

	status = sys_object_new(&__sys_object_class_channel, sizeof(sys_channel_object_t) - sizeof(sys_object_t), (void*)&channel);
	if (status != ferr_ok) {
		goto out;
	}

	channel->channel_did = SYS_CHANNEL_DID_INVALID;

	status = libsyscall_wrapper_server_channel_accept(server_channel->server_did, libsyscall_flags, &channel->channel_did);
	if (status != ferr_ok) {
		goto out;
	}

out:
	if (status == ferr_ok) {
		*out_channel = (void*)channel;
	} else {
		if (channel) {
			sys_release((void*)channel);
		}
	}

	return status;
};

void sys_server_channel_close(sys_server_channel_t* object) {
	sys_server_channel_object_t* server_channel = (void*)object;
	libsyscall_wrapper_server_channel_close(server_channel->server_did, false);
};

ferr_t sys_channel_message_create(size_t initial_length, sys_channel_message_t** out_message) {
	ferr_t status = ferr_ok;
	sys_channel_message_object_t* message = NULL;

	status = sys_object_new(&channel_message_object_class, sizeof(sys_channel_message_object_t) - sizeof(sys_object_t), (void*)&message);
	if (status != ferr_ok) {
		goto out;
	}

	message->body = NULL;
	message->body_length = 0;
	message->owns_body_buffer = true;
	message->attachment_count = 0;
	message->attachments = NULL;
	message->conversation_id = fchannel_conversation_id_none;

	if (initial_length > 0) {
		status = sys_channel_message_extend((void*)message, initial_length);
		if (status != ferr_ok) {
			goto out;
		}
	}

out:
	if (status == ferr_ok) {
		*out_message = (void*)message;
	} else {
		if (message) {
			sys_release((void*)message);
		}
	}

	return status;
};

ferr_t sys_channel_message_create_copy(const void* data, size_t length, sys_channel_message_t** out_message) {
	sys_channel_message_object_t* message = NULL;
	ferr_t status = ferr_ok;

	status = sys_channel_message_create(length, (void*)&message);
	if (status != ferr_ok) {
		goto out;
	}

	simple_memcpy(message->body, data, length);

out:
	if (status == ferr_ok) {
		*out_message = (void*)message;
	} else {
		if (message) {
			sys_release((void*)message);
		}
	}
	return status;
};

ferr_t sys_channel_message_create_nocopy(void* data, size_t length, sys_channel_message_t** out_message) {
	ferr_t status = ferr_ok;
	sys_channel_message_object_t* message = NULL;

	status = sys_object_new(&channel_message_object_class, sizeof(sys_channel_message_object_t) - sizeof(sys_object_t), (void*)&message);
	if (status != ferr_ok) {
		goto out;
	}

	message->body = data;
	message->body_length = length;
	message->owns_body_buffer = false;
	message->attachment_count = 0;
	message->attachments = NULL;
	message->conversation_id = fchannel_conversation_id_none;

out:
	if (status == ferr_ok) {
		*out_message = (void*)message;
	} else {
		if (message) {
			sys_release((void*)message);
		}
	}

	return status;
};

size_t sys_channel_message_length(sys_channel_message_t* object) {
	sys_channel_message_object_t* message = (void*)object;
	return message->body_length;
};

void* sys_channel_message_data(sys_channel_message_t* object) {
	sys_channel_message_object_t* message = (void*)object;
	return message->body;
};

ferr_t sys_channel_message_extend(sys_channel_message_t* object, size_t extra_length) {
	sys_channel_message_object_t* message = (void*)object;
	ferr_t status = ferr_ok;

	if (message->owns_body_buffer) {
		status = sys_mempool_reallocate(message->body, message->body_length + extra_length, NULL, &message->body);
		if (status != ferr_ok) {
			goto out;
		}
	} else {
		void* new_body = NULL;

		status = sys_mempool_allocate(message->body_length + extra_length, NULL, &new_body);
		if (status != ferr_ok) {
			goto out;
		}

		simple_memcpy(new_body, message->body, message->body_length);

		message->body = new_body;
	}

	message->body_length += extra_length;

out:
	return status;
};

static sys_channel_message_attachment_type_t sys_channel_object_class_to_attachment_type(const sys_object_class_t* object_class) {
	if (object_class == &channel_message_attachment_channel_object_class) {
		return sys_channel_message_attachment_type_channel;
	} else if (object_class == sys_object_class_shared_memory()) {
		return sys_channel_message_attachment_type_shared_memory;
	} else {
		return sys_channel_message_attachment_type_invalid;
	}
};

ferr_t sys_channel_message_attach_channel(sys_channel_message_t* object, sys_channel_t* channel_object, sys_channel_message_attachment_index_t* out_attachment_index) {
	sys_channel_message_object_t* message = (void*)object;
	ferr_t status = ferr_ok;
	sys_channel_message_attachment_channel_object_t* attachment = NULL;
	sys_channel_message_attachment_index_t index = UINT64_MAX;
	sys_channel_object_t* channel = (void*)channel_object;

	status = sys_object_new(&channel_message_attachment_channel_object_class, sizeof(sys_channel_message_attachment_channel_object_t) - sizeof(sys_object_t), (void*)&attachment);
	if (status != ferr_ok) {
		goto out;
	}

	attachment->channel_did = SYS_CHANNEL_DID_INVALID;

	status = sys_mempool_reallocate(message->attachments, sizeof(*message->attachments) * (message->attachment_count + 1), NULL, (void*)&message->attachments);
	if (status != ferr_ok) {
		goto out;
	}

	attachment->channel_did = channel->channel_did;
	channel->channel_did = SYS_CHANNEL_DID_INVALID;

	index = message->attachment_count;

	message->attachments[index] = (void*)attachment;

	++message->attachment_count;

out:
	if (status == ferr_ok) {
		if (out_attachment_index) {
			*out_attachment_index = index;
		}

		sys_release(channel_object);
	} else {
		if (attachment) {
			sys_release((void*)attachment);
		}
	}

	return status;
};

ferr_t sys_channel_message_attach_shared_memory(sys_channel_message_t* object, sys_shared_memory_t* shared_memory, sys_channel_message_attachment_index_t* out_attachment_index) {
	sys_channel_message_object_t* message = (void*)object;
	ferr_t status = ferr_ok;
	sys_channel_message_attachment_index_t index = UINT64_MAX;

	if (sys_retain(shared_memory) != ferr_ok) {
		shared_memory = NULL;
		status = ferr_permanent_outage;
		goto out;
	}

	status = sys_mempool_reallocate(message->attachments, sizeof(*message->attachments) * (message->attachment_count + 1), NULL, (void*)&message->attachments);
	if (status != ferr_ok) {
		goto out;
	}

	index = message->attachment_count;

	message->attachments[index] = shared_memory;

	++message->attachment_count;

out:
	if (status == ferr_ok) {
		if (out_attachment_index) {
			*out_attachment_index = index;
		}
	} else {
		if (shared_memory) {
			sys_release((void*)shared_memory);
		}
	}

	return status;
};

size_t sys_channel_message_attachment_count(sys_channel_message_t* object) {
	sys_channel_message_object_t* message = (void*)object;
	return message->attachment_count;
};

sys_channel_message_attachment_type_t sys_channel_message_attachment_type(sys_channel_message_t* object, sys_channel_message_attachment_index_t attachment_index) {
	sys_channel_message_object_t* message = (void*)object;

	if (attachment_index >= message->attachment_count || !message->attachments[attachment_index]) {
		return sys_channel_message_attachment_type_invalid;
	}

	return sys_channel_object_class_to_attachment_type(sys_object_class(message->attachments[attachment_index]));
};

ferr_t sys_channel_message_detach_channel(sys_channel_message_t* object, sys_channel_message_attachment_index_t attachment_index, sys_channel_t** out_channel) {
	sys_channel_message_object_t* message = (void*)object;
	ferr_t status = ferr_ok;
	sys_channel_message_attachment_channel_object_t* attachment = NULL;
	sys_channel_object_t* channel = NULL;

	if (attachment_index >= message->attachment_count) {
		status = ferr_no_such_resource;
		goto out;
	}

	if (sys_channel_message_attachment_type(object, attachment_index) != sys_channel_message_attachment_type_channel) {
		status = ferr_invalid_argument;
		goto out;
	}

	if (out_channel) {
		status = sys_object_new(&__sys_object_class_channel, sizeof(sys_channel_object_t) - sizeof(sys_object_t), (void*)&channel);
		if (status != ferr_ok) {
			goto out;
		}
	}

	attachment = (void*)message->attachments[attachment_index];
	message->attachments[attachment_index] = NULL;

	if (out_channel) {
		channel->channel_did = attachment->channel_did;
	} else {
		libsyscall_wrapper_channel_close(attachment->channel_did, true);
	}
	attachment->channel_did = SYS_CHANNEL_DID_INVALID;

out:
	if (status == ferr_ok) {
		if (out_channel) {
			*out_channel = (void*)channel;
		}
	} else {
		if (channel) {
			sys_release((void*)channel);
		}
	}

	if (attachment) {
		sys_release((void*)attachment);
	}

	return status;
};

ferr_t sys_channel_message_detach_shared_memory(sys_channel_message_t* object, sys_channel_message_attachment_index_t attachment_index, sys_shared_memory_t** out_shared_memory) {
	sys_channel_message_object_t* message = (void*)object;
	ferr_t status = ferr_ok;
	sys_shared_memory_object_t* attachment = NULL;

	if (attachment_index >= message->attachment_count) {
		status = ferr_no_such_resource;
		goto out;
	}

	if (sys_channel_message_attachment_type(object, attachment_index) != sys_channel_message_attachment_type_shared_memory) {
		status = ferr_invalid_argument;
		goto out;
	}

	attachment = (void*)message->attachments[attachment_index];
	message->attachments[attachment_index] = NULL;

	if (out_shared_memory) {
		*out_shared_memory = (void*)attachment;
	} else {
		sys_release((void*)attachment);
	}

out:
	return status;
};

sys_channel_conversation_id_t sys_channel_message_get_conversation_id(sys_channel_message_t* object) {
	sys_channel_message_object_t* message = (void*)object;
	return message->conversation_id;
};

void sys_channel_message_set_conversation_id(sys_channel_message_t* object, sys_channel_conversation_id_t conversation_id) {
	sys_channel_message_object_t* message = (void*)object;
	message->conversation_id = conversation_id;
};

ferr_t sys_channel_message_serialize(sys_channel_message_t* object, libsyscall_channel_message_t* out_syscall_message) {
	sys_channel_message_object_t* message = (void*)object;
	ferr_t status = ferr_ok;
	libsyscall_channel_message_attachment_header_t* current_header = NULL;
	libsyscall_channel_message_attachment_header_t* previous_header = NULL;

	out_syscall_message->conversation_id = message->conversation_id;
	out_syscall_message->body_length = message->body_length;
	out_syscall_message->attachments_length = 0;
	out_syscall_message->message_id = fchannel_message_id_invalid;
	out_syscall_message->attachments_address = (uintptr_t)NULL;

	// we can re-use the same internal body buffer the message object uses
	out_syscall_message->body_address = (uintptr_t)message->body;

	if (message->attachment_count > 0) {
		// determine the required length for the attachments buffer
		for (size_t i = 0; i < message->attachment_count; ++i) {
			sys_object_t* attachment = message->attachments[i];

			if (!attachment) {
				// we serialize NULL values as null attachments
				// (this is done so that the indices we return to callers when they add an attachment remain valid even when
				// other attachments are added or removed)
				out_syscall_message->attachments_length += sizeof(libsyscall_channel_message_attachment_null_t);
				continue;
			}

			switch (sys_channel_object_class_to_attachment_type(sys_object_class(attachment))) {
				case sys_channel_message_attachment_type_channel:
					out_syscall_message->attachments_length += sizeof(libsyscall_channel_message_attachment_channel_t);
					break;

				case sys_channel_message_attachment_type_shared_memory:
					out_syscall_message->attachments_length += sizeof(libsyscall_channel_message_attachment_mapping_t);
					break;

				default:
					// bad attachment type
					sys_abort();
			}
		}

		// now allocate the attachments buffer
		status = sys_mempool_allocate(out_syscall_message->attachments_length, (void*)&out_syscall_message->attachments_length, (void*)&out_syscall_message->attachments_address);
		if (status != ferr_ok) {
			goto out;
		}

		// clear it
		simple_memset((void*)out_syscall_message->attachments_address, 0, out_syscall_message->attachments_length);

		// and finally, populate it
		current_header = (void*)out_syscall_message->attachments_address;
		for (size_t i = 0; i < message->attachment_count; (++i), (previous_header = current_header), (current_header = (void*)((char*)current_header + current_header->length))) {
			sys_object_t* attachment = message->attachments[i];

			if (previous_header) {
				previous_header->next_offset = (uintptr_t)current_header - (uintptr_t)previous_header;
			}

			if (!attachment) {
				current_header->type = fchannel_message_attachment_type_null;
				current_header->length = sizeof(libsyscall_channel_message_attachment_null_t);
				continue;
			}

			switch (sys_channel_object_class_to_attachment_type(sys_object_class(attachment))) {
				case sys_channel_message_attachment_type_channel: {
					libsyscall_channel_message_attachment_channel_t* channel_attachment = (void*)current_header;
					sys_channel_message_attachment_channel_object_t* channel_attachment_object = (void*)attachment;

					channel_attachment->header.type = fchannel_message_attachment_type_channel;
					channel_attachment->header.length = sizeof(*channel_attachment);
					channel_attachment->channel_id = channel_attachment_object->channel_did;
				} break;

				case sys_channel_message_attachment_type_shared_memory: {
					libsyscall_channel_message_attachment_mapping_t* mapping_attachment = (void*)current_header;
					sys_shared_memory_object_t* shared_memory_object = (void*)attachment;

					mapping_attachment->header.type = fchannel_message_attachment_type_mapping;
					mapping_attachment->header.length = sizeof(*mapping_attachment);
					mapping_attachment->mapping_id = shared_memory_object->did;
				} break;

				default:
					// bad attachment type
					sys_abort();
			}
		}
	}

out:
	if (status != ferr_ok) {
		if ((void*)out_syscall_message->attachments_address != NULL) {
			LIBSYS_WUR_IGNORE(sys_mempool_free((void*)out_syscall_message->attachments_address));
		}
	}

	return status;
};

void sys_channel_message_consumed(sys_channel_message_t* object, libsyscall_channel_message_t* in_syscall_message) {
	sys_channel_message_object_t* message = (void*)object;

	// let's clean up the message now that's it been consumed by the kernel

	// free the body buffer
	LIBSYS_WUR_IGNORE(sys_mempool_free(message->body));
	message->body = NULL;

	if (message->attachment_count > 0) {
		// free the syscall attachment buffer
		LIBSYS_WUR_IGNORE(sys_mempool_free((void*)in_syscall_message->attachments_address));

		// clean up message attachments
		for (size_t i = 0; i < message->attachment_count; ++i) {
			sys_object_t* attachment = message->attachments[i];

			if (!attachment) {
				continue;
			}

			switch (sys_channel_object_class_to_attachment_type(sys_object_class(attachment))) {
				case sys_channel_message_attachment_type_channel: {
					sys_channel_message_attachment_channel_object_t* channel_attachment_object = (void*)attachment;

					// the channel descriptor has been consumed by the kernel, so we don't need to (and shouldn't) close it ourselves
					channel_attachment_object->channel_did = SYS_CHANNEL_DID_INVALID;
				} break;

				case sys_channel_message_attachment_type_shared_memory:
					// the mapping was simply retained by the kernel;
					// our mapping descriptor is still perfectly valid
					break;

				default:
					// bad attachment type
					sys_abort();
			}

			sys_release(attachment);
		}

		// free the attachments buffer
		LIBSYS_WUR_IGNORE(sys_mempool_free(message->attachments));
		message->attachments = NULL;
		message->attachment_count = 0;
	}

	// now release the message object
	sys_release(object);
};

ferr_t sys_channel_message_deserialize_begin(sys_channel_message_deserialization_context_t* out_context) {
	ferr_t status = ferr_ok;

	out_context->message = NULL;

	out_context->syscall_message.conversation_id = fchannel_conversation_id_none;
	out_context->syscall_message.message_id = fchannel_message_id_invalid;
	out_context->syscall_message.body_length = SYS_CHANNEL_DEFAULT_SYSCALL_BODY_BUFFER_SIZE;
	out_context->syscall_message.attachments_length = SYS_CHANNEL_DEFAULT_SYSCALL_ATTACHMENT_BUFFER_SIZE;
	out_context->syscall_message.body_address = (uintptr_t)NULL;
	out_context->syscall_message.attachments_address = (uintptr_t)NULL;

retry:
	if (out_context->syscall_message.body_length < SYS_CHANNEL_MINIMUM_SYSCALL_BODY_BUFFER_SIZE || out_context->syscall_message.attachments_length < SYS_CHANNEL_MINIMUM_SYSCALL_ATTACHMENT_BUFFER_SIZE) {
		status = ferr_temporary_outage;
		goto out;
	}

	if (out_context->syscall_message.body_length > 0 && (void*)out_context->syscall_message.body_address == NULL) {
		status = sys_mempool_allocate(out_context->syscall_message.body_length, (void*)&out_context->syscall_message.body_length, (void*)&out_context->syscall_message.body_address);
		if (status != ferr_ok) {
			out_context->syscall_message.body_length /= 2;
			goto retry;
		}
	}

	if (out_context->syscall_message.attachments_length > 0 && (void*)out_context->syscall_message.attachments_address == NULL) {
		status = sys_mempool_allocate(out_context->syscall_message.attachments_length, (void*)&out_context->syscall_message.attachments_length, (void*)&out_context->syscall_message.attachments_address);
		if (status != ferr_ok) {
			out_context->syscall_message.attachments_length /= 2;
			goto retry;
		}
	}

out:
	if (status != ferr_ok) {
		if ((void*)out_context->syscall_message.body_address != NULL) {
			LIBSYS_WUR_IGNORE(sys_mempool_free((void*)out_context->syscall_message.body_address));
		}

		if ((void*)out_context->syscall_message.attachments_address != NULL) {
			LIBSYS_WUR_IGNORE(sys_mempool_free((void*)out_context->syscall_message.attachments_address));
		}
	}

	return status;
};

ferr_t sys_channel_message_deserialize_resize(sys_channel_message_deserialization_context_t* in_out_context) {
	ferr_t status = ferr_ok;

	status = sys_mempool_reallocate((void*)in_out_context->syscall_message.body_address, in_out_context->syscall_message.body_length, NULL, (void*)&in_out_context->syscall_message.body_address);
	if (status != ferr_ok) {
		goto out;
	}

	status = sys_mempool_reallocate((void*)in_out_context->syscall_message.attachments_address, in_out_context->syscall_message.attachments_length, NULL, (void*)&in_out_context->syscall_message.attachments_address);
	if (status != ferr_ok) {
		goto out;
	}

out:
	return status;
};

ferr_t sys_channel_message_deserialize_prepare(sys_channel_message_deserialization_context_t* in_out_context) {
	ferr_t status = ferr_ok;
	sys_channel_message_object_t* message = NULL;
	size_t attachment_count = 0;

	status = sys_object_new(&channel_message_object_class, sizeof(*message) - sizeof(message->object), (void*)&message);
	if (status != ferr_ok) {
		goto out;
	}

	message->conversation_id = fchannel_conversation_id_none;
	message->attachment_count = 0;
	message->attachments = NULL;
	message->body = NULL;
	message->body_length = in_out_context->syscall_message.body_length;

	message->body = (void*)in_out_context->syscall_message.body_address;
	message->owns_body_buffer = false;

	// try to shrink the body buffer to match the returned length, but don't worry if it fails
	LIBSYS_WUR_IGNORE(sys_mempool_reallocate(message->body, message->body_length, NULL, &message->body));
	in_out_context->syscall_message.body_address = (uintptr_t)message->body;

	for (libsyscall_channel_message_attachment_header_t* header = (void*)in_out_context->syscall_message.attachments_address; header != NULL && (uintptr_t)header - in_out_context->syscall_message.attachments_address < in_out_context->syscall_message.attachments_length; header = (header->next_offset == 0) ? NULL : (void*)((char*)header + header->next_offset)) {
		++attachment_count;
	}

	if (attachment_count > 0) {
		status = sys_mempool_allocate(sizeof(*message->attachments) * attachment_count, NULL, (void*)&message->attachments);
		if (status != ferr_ok) {
			goto out;
		}

		simple_memset(message->attachments, 0, sizeof(*message->attachments) * attachment_count);

		for (libsyscall_channel_message_attachment_header_t* header = (void*)in_out_context->syscall_message.attachments_address; header != NULL && (uintptr_t)header - in_out_context->syscall_message.attachments_address < in_out_context->syscall_message.attachments_length; header = (header->next_offset == 0) ? NULL : (void*)((char*)header + header->next_offset)) {
			switch (header->type) {
				case fchannel_message_attachment_type_channel: {
					sys_channel_message_attachment_channel_object_t* attachment_object = NULL;

					status = sys_object_new(&channel_message_attachment_channel_object_class, sizeof(sys_channel_message_attachment_channel_object_t) - sizeof(sys_object_t), (void*)&attachment_object);
					if (status != ferr_ok) {
						goto out;
					}

					attachment_object->channel_did = SYS_CHANNEL_DID_INVALID;

					message->attachments[message->attachment_count] = (void*)attachment_object;
					++message->attachment_count;
				} break;

				case fchannel_message_attachment_type_mapping: {
					sys_shared_memory_object_t* attachment_object = NULL;

					status = sys_object_new(sys_object_class_shared_memory(), sizeof(*attachment_object) - sizeof(attachment_object->object), (void*)&attachment_object);
					if (status != ferr_ok) {
						goto out;
					}

					attachment_object->did = UINT64_MAX;

					message->attachments[message->attachment_count] = (void*)attachment_object;
					++message->attachment_count;
				} break;

				case fchannel_message_attachment_type_null: {
					// leave the entry in the attachment array as `NULL`
					++message->attachment_count;
				} break;

				default:
					// bad attachment type
					sys_abort();
			}
		}
	}

	in_out_context->message = (void*)message;

out:
	if (status != ferr_ok) {
		if (message) {
			sys_release((void*)message);
		}
	}

	return status;
};

void sys_channel_message_deserialize_abort(sys_channel_message_deserialization_context_t* in_context) {
	if ((void*)in_context->syscall_message.body_address != NULL) {
		LIBSYS_WUR_IGNORE(sys_mempool_free((void*)in_context->syscall_message.body_address));
	}

	if ((void*)in_context->syscall_message.attachments_address != NULL) {
		LIBSYS_WUR_IGNORE(sys_mempool_free((void*)in_context->syscall_message.attachments_address));
	}

	if (in_context->message) {
		sys_release(in_context->message);
	}
};

void sys_channel_message_deserialize_finalize(sys_channel_message_deserialization_context_t* in_context, sys_channel_message_t** out_message) {
	size_t attachment_index = 0;
	sys_channel_message_object_t* message = (void*)in_context->message;

	// assign the conversation ID
	message->conversation_id = in_context->syscall_message.conversation_id;

	for (libsyscall_channel_message_attachment_header_t* header = (void*)in_context->syscall_message.attachments_address; header != NULL && (uintptr_t)header - in_context->syscall_message.attachments_address < in_context->syscall_message.attachments_length; header = (header->next_offset == 0) ? NULL : (void*)((char*)header + header->next_offset)) {
		switch (header->type) {
			case fchannel_message_attachment_type_channel: {
				libsyscall_channel_message_attachment_channel_t* syscall_attachment = (void*)header;
				sys_channel_message_attachment_channel_object_t* attachment_object = (void*)message->attachments[attachment_index];

				attachment_object->channel_did = syscall_attachment->channel_id;

				++attachment_index;
			} break;

			case fchannel_message_attachment_type_mapping: {
				libsyscall_channel_message_attachment_mapping_t* syscall_attachment = (void*)header;
				sys_shared_memory_object_t* attachment_object = (void*)message->attachments[attachment_index];

				attachment_object->did = syscall_attachment->mapping_id;

				++attachment_index;
			} break;

			case fchannel_message_attachment_type_null:
				++attachment_index;
				break;

			default:
				// bad attachment type
				sys_abort();
		}
	}

	// free the attachments buffer
	if ((void*)in_context->syscall_message.attachments_address != NULL) {
		LIBSYS_WUR_IGNORE(sys_mempool_free((void*)in_context->syscall_message.attachments_address));
	}

	// transfer ownership of the body buffer into the message object
	message->owns_body_buffer = true;

	if (out_message) {
		*out_message = in_context->message;
	} else {
		sys_release(in_context->message);
	}
};
