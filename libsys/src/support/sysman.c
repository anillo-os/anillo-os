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

#include <libsys/channels.private.h>
#include <libsys/sysman.private.h>
#include <libsimple/libsimple.h>
#include <libeve/libeve.h>

LIBSYS_STRUCT(sys_sysman_connect_async_context) {
	sys_channel_connect_async_callback_f callback;
	void* context;
};

LIBSYS_STRUCT(sys_sysman_register_async_context) {
	sys_sysman_register_callback_f callback;
	void* context;
};

static sys_channel_object_t sysman_channel = {
	.object = {
		.object_class = &__sys_object_class_channel,
		.reference_count = 0,
		.flags = sys_object_flag_immortal,
	},

	// the sysman channel is always DID 2
	.channel_did = 2,
};

eve_channel_t* __sys_sysman_eve_channel = NULL;

ferr_t sys_sysman_init(void) {
#if BUILDING_DYMPLE || BUILDING_STATIC
	return ferr_ok;
#else
	ferr_t status = ferr_ok;

	status = eve_channel_create((void*)&sysman_channel, NULL, &__sys_sysman_eve_channel);
	if (status != ferr_ok) {
		goto out;
	}

	status = eve_loop_add_item(eve_loop_get_main(), __sys_sysman_eve_channel);
	if (status != ferr_ok) {
		goto out;
	}

out:
	return status;
#endif
};

static ferr_t sys_channel_connect_create_message(const char* server_name, size_t server_name_length, sys_channel_message_t** out_message) {
	ferr_t status = ferr_ok;
	sys_channel_message_t* message = NULL;
	sys_sysman_rpc_call_connect_t* rpc = NULL;
	sys_channel_conversation_id_t convo_id = sys_channel_conversation_id_none;

	status = sys_channel_message_create(sizeof(*rpc) + server_name_length, &message);
	if (status != ferr_ok) {
		goto out;
	}

	rpc = sys_channel_message_data(message);

#if BUILDING_DYMPLE
	status = sys_channel_conversation_create((void*)&sysman_channel, &convo_id);
#else
	status = eve_channel_conversation_create(__sys_sysman_eve_channel, &convo_id);
#endif
	if (status != ferr_ok) {
		goto out;
	}

	sys_channel_message_set_conversation_id(message, convo_id);

	rpc->header.function = sys_sysman_rpc_function_connect;
	simple_memcpy(rpc->name, server_name, server_name_length);

out:
	if (status == ferr_ok) {
		*out_message = message;
	} else {
		if (message) {
			sys_release(message);
		}
	}
	return status;
};

#if !BUILDING_DYMPLE
static void sys_channel_connect_async_reply_handler(void* context, eve_channel_t* channel, sys_channel_message_t* message, ferr_t status) {
	sys_sysman_connect_async_context_t* async_context = context;
	sys_channel_t* received_channel = NULL;

	if (status == ferr_ok) {
		status = sys_channel_message_detach_channel(message, 0, &received_channel);
	}

	if (message) {
		sys_release(message);
	}

	async_context->callback(async_context->context, received_channel);

	LIBSYS_WUR_IGNORE(sys_mempool_free(async_context));
};
#endif

ferr_t sys_channel_connect_async(const char* server_name, sys_channel_connect_async_callback_f callback, void* context) {
	return sys_channel_connect_async_n(server_name, simple_strlen(server_name), callback, context);
};

ferr_t sys_channel_connect_async_n(const char* server_name, size_t server_name_length, sys_channel_connect_async_callback_f callback, void* context) {
#if BUILDING_DYMPLE
	return ferr_unsupported;
#else
	ferr_t status = ferr_ok;
	sys_channel_message_t* message = NULL;
	sys_sysman_connect_async_context_t* async_context = NULL;

	status = sys_mempool_allocate(sizeof(*async_context), NULL, (void*)&async_context);
	if (status != ferr_ok) {
		goto out;
	}

	async_context->callback = callback;
	async_context->context = context;

	status = sys_channel_connect_create_message(server_name, server_name_length, &message);
	if (status != ferr_ok) {
		goto out;
	}

	status = eve_channel_send_with_reply_async(__sys_sysman_eve_channel, message, sys_channel_connect_async_reply_handler, async_context);
	if (status != ferr_ok) {
		goto out;
	}

	// successfully sending the message consumes it
	message = NULL;

out:
	if (status != ferr_ok) {
		if (async_context) {
			LIBSYS_WUR_IGNORE(sys_mempool_free(async_context));
		}
	}
	if (message) {
		sys_release(message);
	}
	return status;
#endif
};

ferr_t sys_channel_connect_sync(const char* server_name, sys_channel_t** out_channel) {
	return sys_channel_connect_sync_n(server_name, simple_strlen(server_name), out_channel);
};

ferr_t sys_channel_connect_sync_n(const char* server_name, size_t server_name_length, sys_channel_t** out_channel) {
	ferr_t status = ferr_ok;
	sys_channel_message_t* message = NULL;
	sys_channel_message_t* reply = NULL;

	status = sys_channel_connect_create_message(server_name, server_name_length, &message);
	if (status != ferr_ok) {
		goto out;
	}

#if BUILDING_DYMPLE
	status = sys_channel_send((void*)&sysman_channel, 0, message, NULL);
#else
	status = eve_channel_send_with_reply_sync(__sys_sysman_eve_channel, message, &reply);
#endif
	if (status != ferr_ok) {
		goto out;
	}

	// successfully sending the message consumes it
	message = NULL;

#if BUILDING_DYMPLE
	status = sys_channel_receive((void*)&sysman_channel, 0, &reply);
	if (status != ferr_ok) {
		goto out;
	}
#endif

	status = sys_channel_message_detach_channel(reply, 0, out_channel);
	if (status != ferr_ok) {
		goto out;
	}

out:
	if (message) {
		sys_release(message);
	}
	if (reply) {
		sys_release(reply);
	}
	return status;
};

#if !BUILDING_DYMPLE
static ferr_t sys_sysman_register_create_message(const char* server_name, size_t server_name_length, sys_sysman_realm_t realm, sys_channel_message_t** out_message) {
	ferr_t status = ferr_ok;
	sys_channel_message_t* message = NULL;
	sys_sysman_rpc_call_register_t* rpc = NULL;
	sys_channel_conversation_id_t convo_id = sys_channel_conversation_id_none;

	status = sys_channel_message_create(sizeof(*rpc) + server_name_length, &message);
	if (status != ferr_ok) {
		goto out;
	}

	rpc = sys_channel_message_data(message);

	status = eve_channel_conversation_create(__sys_sysman_eve_channel, &convo_id);
	if (status != ferr_ok) {
		goto out;
	}

	sys_channel_message_set_conversation_id(message, convo_id);

	rpc->header.function = sys_sysman_rpc_function_connect;
	rpc->realm = realm;
	simple_memcpy(rpc->name, server_name, server_name_length);

out:
	if (status == ferr_ok) {
		*out_message = message;
	} else {
		if (message) {
			sys_release(message);
		}
	}
	return status;
};

static void sys_sysman_register_async_reply_handler(void* context, eve_channel_t* channel, sys_channel_message_t* message, ferr_t status) {
	sys_sysman_register_async_context_t* async_context = context;
	sys_channel_t* received_channel = NULL;

	if (status == ferr_ok) {
		status = sys_channel_message_detach_channel(message, 0, &received_channel);
	}

	if (message) {
		sys_release(message);
	}

	async_context->callback(async_context->context, received_channel);

	LIBSYS_WUR_IGNORE(sys_mempool_free(async_context));
};
#endif

ferr_t sys_sysman_register_sync(const char* name, sys_sysman_realm_t realm, sys_channel_t** out_server_channel) {
	return sys_sysman_register_sync_n(name, simple_strlen(name), realm, out_server_channel);
};

ferr_t sys_sysman_register_sync_n(const char* name, size_t name_length, sys_sysman_realm_t realm, sys_channel_t** out_server_channel) {
#if BUILDING_DYMPLE
	return ferr_unsupported;
#else
	ferr_t status = ferr_ok;
	sys_channel_message_t* message = NULL;
	sys_channel_message_t* reply = NULL;

	status = sys_sysman_register_create_message(name, name_length, realm, &message);
	if (status != ferr_ok) {
		goto out;
	}

	status = eve_channel_send_with_reply_sync(__sys_sysman_eve_channel, message, &reply);
	if (status != ferr_ok) {
		goto out;
	}

	// successfully sending the message consumes it
	message = NULL;

	status = sys_channel_message_detach_channel(reply, 0, out_server_channel);
	if (status != ferr_ok) {
		goto out;
	}

out:
	if (message) {
		sys_release(message);
	}
	if (reply) {
		sys_release(reply);
	}
	return status;
#endif
};

ferr_t sys_sysman_register_async(const char* name, sys_sysman_realm_t realm, sys_sysman_register_callback_f callback, void* context) {
	return sys_sysman_register_async_n(name, simple_strlen(name), realm, callback, context);
};

ferr_t sys_sysman_register_async_n(const char* name, size_t name_length, sys_sysman_realm_t realm, sys_sysman_register_callback_f callback, void* context) {
#if BUILDING_DYMPLE
	return ferr_unsupported;
#else
	ferr_t status = ferr_ok;
	sys_channel_message_t* message = NULL;
	sys_sysman_register_async_context_t* async_context = NULL;

	status = sys_mempool_allocate(sizeof(*async_context), NULL, (void*)&async_context);
	if (status != ferr_ok) {
		goto out;
	}

	async_context->callback = callback;
	async_context->context = context;

	status = sys_sysman_register_create_message(name, name_length, realm, &message);
	if (status != ferr_ok) {
		goto out;
	}

	status = eve_channel_send_with_reply_async(__sys_sysman_eve_channel, message, sys_sysman_register_async_reply_handler, async_context);
	if (status != ferr_ok) {
		goto out;
	}

	// successfully sending the message consumes it
	message = NULL;

out:
	if (status != ferr_ok) {
		if (async_context) {
			LIBSYS_WUR_IGNORE(sys_mempool_free(async_context));
		}
	}
	if (message) {
		sys_release(message);
	}
	return status;
#endif
};

#if !BUILDING_DYMPLE
static ferr_t sys_sysman_subchannel_create_message(sys_channel_message_t** out_message) {
	ferr_t status = ferr_ok;
	sys_channel_message_t* message = NULL;
	sys_sysman_rpc_call_subchannel_t* rpc = NULL;
	sys_channel_conversation_id_t convo_id = sys_channel_conversation_id_none;

	status = sys_channel_message_create(sizeof(*rpc), &message);
	if (status != ferr_ok) {
		goto out;
	}

	rpc = sys_channel_message_data(message);

	status = eve_channel_conversation_create(__sys_sysman_eve_channel, &convo_id);
	if (status != ferr_ok) {
		goto out;
	}

	sys_channel_message_set_conversation_id(message, convo_id);

	rpc->header.function = sys_sysman_rpc_function_subchannel;

out:
	if (status == ferr_ok) {
		*out_message = message;
	} else {
		if (message) {
			sys_release(message);
		}
	}
	return status;
};
#endif

// sysman (the only user of the static library) defines `sys_sysman_create_subchannel` itself
#if !BUILDING_STATIC
ferr_t sys_sysman_create_subchannel(sys_channel_t** out_subchannel) {
#if BUILDING_DYMPLE
	return ferr_unsupported;
#else
	ferr_t status = ferr_ok;
	sys_channel_message_t* message = NULL;
	sys_channel_message_t* reply = NULL;

	status = sys_sysman_subchannel_create_message(&message);
	if (status != ferr_ok) {
		goto out;
	}

	status = eve_channel_send_with_reply_sync(__sys_sysman_eve_channel, message, &reply);
	if (status != ferr_ok) {
		goto out;
	}

	// successfully sending the message consumes it
	message = NULL;

	status = sys_channel_message_detach_channel(reply, 0, out_subchannel);
	if (status != ferr_ok) {
		goto out;
	}

out:
	if (message) {
		sys_release(message);
	}
	if (reply) {
		sys_release(reply);
	}
	return status;
#endif
};
#endif
