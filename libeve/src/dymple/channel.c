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

#include <libeve/channel.private.h>
#include <libeve/item.private.h>

static void eve_channel_destroy(sys_object_t* obj) {
	eve_channel_object_t* channel = (void*)obj;

	if (channel->destructor) {
		channel->destructor(channel->context);
	}

	if (channel->sys_channel) {
		sys_release(channel->sys_channel);
	}

	sys_object_destroy(obj);
};

static void eve_channel_set_destructor(eve_item_t* self, eve_item_destructor_f destructor) {
	eve_channel_object_t* channel = (void*)self;
	channel->destructor = destructor;
};

static void* eve_channel_get_context(eve_item_t* self) {
	eve_channel_object_t* channel = (void*)self;
	return channel->context;
};

static const eve_item_interface_t eve_channel_item = {
	LIBEVE_ITEM_INTERFACE(NULL),
	.handle_events = NULL,
	.get_monitor_item = NULL,
	.poll_after_attach = NULL,
	.set_destructor = eve_channel_set_destructor,
	.get_context = eve_channel_get_context,
};

static const eve_object_class_t eve_channel_class = {
	LIBSYS_OBJECT_CLASS_INTERFACE(&eve_channel_item.interface),
	.destroy = eve_channel_destroy,
};

const eve_object_class_t* eve_object_class_channel(void) {
	return &eve_channel_class;
};

ferr_t eve_channel_create(sys_channel_t* sys_channel, void* context, eve_channel_t** out_channel) {
	ferr_t status = ferr_ok;
	eve_channel_object_t* channel = NULL;

	if (sys_retain(sys_channel) != ferr_ok) {
		sys_channel = NULL;
		status = ferr_permanent_outage;
		goto out;
	}

	status = sys_object_new(&eve_channel_class, sizeof(*channel) - sizeof(channel->object), (void*)&channel);
	if (status != ferr_ok) {
		goto out;
	}

	simple_memset((char*)channel + sizeof(channel->object), 0, sizeof(*channel) - sizeof(channel->object));

	channel->sys_channel = sys_channel;
	channel->context = context;

out:
	if (status == ferr_ok) {
		*out_channel = (void*)channel;
	} else if (channel) {
		eve_release((void*)channel);
	} else {
		if (sys_channel) {
			sys_release(sys_channel);
		}
	}
	return status;
};

void eve_channel_set_message_handler(eve_channel_t* obj, eve_channel_message_handler_f handler) {
	eve_channel_object_t* channel = (void*)obj;
	channel->message_handler = handler;
};

void eve_channel_set_peer_close_handler(eve_channel_t* obj, eve_channel_peer_close_handler_f handler) {
	eve_channel_object_t* channel = (void*)obj;
	channel->peer_close_handler = handler;
};

void eve_channel_set_message_send_error_handler(eve_channel_t* obj, eve_channel_message_send_error_handler_f handler) {
	eve_channel_object_t* channel = (void*)obj;
	channel->message_send_error_handler = handler;
};

ferr_t eve_channel_send(eve_channel_t* obj, sys_channel_message_t* message, bool synchronous) {
	eve_channel_object_t* channel = (void*)obj;
	ferr_t status = ferr_ok;

	if (!synchronous) {
		status = ferr_unsupported;
		goto out;
	}

	status = sys_channel_send(channel->sys_channel, 0, message, NULL);
	if (status != ferr_ok) {
		goto out;
	}

out:
	return status;
};

ferr_t eve_channel_target(eve_channel_t* obj, bool retain, sys_channel_t** out_sys_channel) {
	eve_channel_object_t* channel = (void*)obj;

	if (retain && sys_retain(channel->sys_channel) != ferr_ok) {
		return ferr_permanent_outage;
	}

	*out_sys_channel = channel->sys_channel;
	return ferr_ok;
};

ferr_t eve_channel_conversation_create(eve_channel_t* obj, sys_channel_conversation_id_t* out_conversation_id) {
	eve_channel_object_t* channel = (void*)obj;
	return sys_channel_conversation_create(channel->sys_channel, out_conversation_id);
};

ferr_t eve_channel_send_with_reply_sync(eve_channel_t* obj, sys_channel_message_t* message, sys_channel_message_t** out_reply) {
	eve_channel_object_t* channel = (void*)obj;
	ferr_t status = ferr_ok;
	sys_channel_t* reply = NULL;
	sys_channel_conversation_id_t convo_id = sys_channel_message_get_conversation_id(message);

	if (convo_id == sys_channel_conversation_id_none) {
		status = ferr_invalid_argument;
		goto out;
	}

	status = sys_channel_send(channel->sys_channel, 0, message, NULL);
	if (status != ferr_ok) {
		goto out;
	}

	status = eve_channel_receive_conversation_sync(obj, convo_id, out_reply);
	if (status != ferr_ok) {
		goto out;
	}

out:
	return status;
};

ferr_t eve_channel_receive_conversation_sync(eve_channel_t* obj, sys_channel_conversation_id_t conversation_id, sys_channel_message_t** out_reply) {
	eve_channel_object_t* channel = (void*)obj;
	ferr_t status = ferr_ok;
	sys_channel_message_t* message = NULL;

	status = sys_channel_receive(channel->sys_channel, 0, &message);
	if (status != ferr_ok) {
		goto out;
	}

	if (sys_channel_message_get_conversation_id(message) != conversation_id) {
		// no way to handle this gracefully, but this shouldn't happen in dymple anyways
		sys_abort();
	}

out:
	if (status == ferr_ok) {
		*out_reply = message;
	} else if (message) {
		sys_release(message);
	}
	return status;
};

//
// unsupported APIs
//

ferr_t eve_channel_send_with_reply_async(eve_channel_t* obj, sys_channel_message_t* message, eve_channel_reply_handler_f reply_handler, void* context) {
	return ferr_unsupported;
};

ferr_t eve_channel_receive_conversation_async(eve_channel_t* obj, sys_channel_conversation_id_t conversation_id, eve_channel_reply_handler_f reply_handler, void* context, eve_channel_cancellation_token_t* out_cancellation_token) {
	return ferr_unsupported;
};

ferr_t eve_channel_receive_conversation_cancel(eve_channel_t* obj, sys_channel_conversation_id_t conversation_id, eve_channel_cancellation_token_t cancellation_token) {
	return ferr_unsupported;
};
