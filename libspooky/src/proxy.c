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

#include <libspooky/proxy.private.h>
#include <libspooky/deserializer.private.h>
#include <libspooky/invocation.private.h>
#include <libspooky/types.private.h>

static spooky_type_object_t proxy_type = {
	.object = {
		.object_class = &spooky_direct_object_class_type,
		.reference_count = 0,
		.flags = 0,
	},
	.byte_size = sizeof(spooky_proxy_t*),
	.global = true,
};

spooky_type_t* spooky_type_proxy(void) {
	return (void*)&proxy_type;
};

static void spooky_proxy_interface_destroy(spooky_object_t* obj) {
	spooky_proxy_interface_object_t* proxy_interface = (void*)obj;

	if (proxy_interface->entries) {
		LIBSPOOKY_WUR_IGNORE(sys_mempool_free(proxy_interface->entries));
	}

	sys_object_destroy(obj);
};

static const spooky_object_class_t proxy_interface_class = {
	LIBSYS_OBJECT_CLASS_INTERFACE(NULL),
	.destroy = spooky_proxy_interface_destroy,
};

ferr_t spooky_proxy_interface_create(const spooky_proxy_interface_entry_t* entries, size_t entry_count, spooky_proxy_interface_t** out_proxy_interface) {
	ferr_t status = ferr_ok;
	spooky_proxy_interface_object_t* proxy_interface = NULL;

	status = sys_object_new(&proxy_interface_class, sizeof(*proxy_interface) - sizeof(proxy_interface->object), (void*)&proxy_interface);
	if (status != ferr_ok) {
		goto out;
	}

	proxy_interface->entry_count = 0;
	proxy_interface->entries = NULL;

	status = sys_mempool_allocate(sizeof(*entries) * entry_count, NULL, (void*)&proxy_interface->entries);
	if (status != ferr_ok) {
		goto out;
	}

	proxy_interface->entry_count = entry_count;
	simple_memcpy(proxy_interface->entries, entries, sizeof(*entries) * entry_count);

out:
	if (status == ferr_ok) {
		*out_proxy_interface = (void*)proxy_interface;
	} else if (proxy_interface) {
		spooky_release((void*)proxy_interface);
	}
	return status;
};

static void spooky_proxy_destroy(spooky_object_t* obj) {
	spooky_proxy_object_t* proxy = (void*)obj;
	sys_object_destroy(obj);
};

static const spooky_object_class_t proxy_class = {
	LIBSYS_OBJECT_CLASS_INTERFACE(NULL),
	.destroy = spooky_proxy_destroy,
};

static void spooky_incoming_proxy_destroy(spooky_object_t* obj) {
	spooky_incoming_proxy_object_t* incoming_proxy = (void*)obj;

	if (incoming_proxy->channel) {
		LIBSPOOKY_WUR_IGNORE(eve_loop_remove_item(incoming_proxy->loop, incoming_proxy->channel));

		eve_release(incoming_proxy->channel);
	}

	if (incoming_proxy->loop) {
		eve_release(incoming_proxy->loop);
	}

	spooky_proxy_destroy(obj);
};

static const spooky_object_class_t incoming_proxy_class = {
	LIBSYS_OBJECT_CLASS_INTERFACE(NULL),
	.destroy = spooky_incoming_proxy_destroy,
};

static void spooky_outgoing_proxy_destroy(spooky_object_t* obj) {
	spooky_outgoing_proxy_object_t* outgoing_proxy = (void*)obj;

	if (outgoing_proxy->destructor) {
		outgoing_proxy->destructor(outgoing_proxy->context);
	}

	if (outgoing_proxy->interface) {
		spooky_release((void*)outgoing_proxy->interface);
	}

	spooky_proxy_destroy(obj);
};

static const spooky_object_class_t outgoing_proxy_class = {
	LIBSYS_OBJECT_CLASS_INTERFACE(NULL),
	.destroy = spooky_outgoing_proxy_destroy,
};

bool spooky_proxy_is_incoming(spooky_proxy_t* obj) {
	return obj->object_class == &incoming_proxy_class;
};

ferr_t spooky_proxy_create(spooky_proxy_interface_t* interface, void* context, spooky_proxy_destructor_f destructor, spooky_proxy_t** out_proxy) {
	ferr_t status = ferr_ok;
	spooky_outgoing_proxy_object_t* outgoing_proxy = NULL;

	status = spooky_retain(interface);
	if (status != ferr_ok) {
		interface = NULL;
		goto out;
	}

	status = sys_object_new(&outgoing_proxy_class, sizeof(*outgoing_proxy) - sizeof(outgoing_proxy->base.object), (void*)&outgoing_proxy);
	if (status != ferr_ok) {
		goto out;
	}

	outgoing_proxy->interface = (void*)interface;
	outgoing_proxy->destructor = destructor;
	outgoing_proxy->context = context;

out:
	if (status == ferr_ok) {
		*out_proxy = (void*)outgoing_proxy;
	} else if (outgoing_proxy) {
		spooky_release((void*)outgoing_proxy);
	} else {
		if (interface) {
			spooky_release(interface);
		}
	}
	return status;
};

static void incoming_proxy_peer_close_handler(void* context, eve_channel_t* channel) {
	LIBSPOOKY_WUR_IGNORE(eve_loop_remove_item(eve_loop_get_current(), channel));
};

ferr_t spooky_proxy_create_incoming(sys_channel_t* sys_channel, eve_loop_t* loop, spooky_proxy_t** out_proxy) {
	ferr_t status = ferr_ok;
	spooky_incoming_proxy_object_t* incoming_proxy = NULL;

	status = eve_retain(loop);
	if (status != ferr_ok) {
		goto out;
	}

	status = sys_object_new(&incoming_proxy_class, sizeof(*incoming_proxy) - sizeof(incoming_proxy->base.object), (void*)&incoming_proxy);
	if (status != ferr_ok) {
		eve_release(loop);
		goto out;
	}

	incoming_proxy->loop = loop;
	incoming_proxy->channel = NULL;

	status = eve_channel_create(sys_channel, NULL, &incoming_proxy->channel);
	if (status != ferr_ok) {
		goto out;
	}

	// the libeve channel should now have the only reference to the libsys channel
	sys_release(sys_channel);

	// no need to install some handlers for the channel;
	//   1. we don't need a message handler since we don't expect to receive messages that aren't replies to messages we sent
	//      (by not setting a handler, non-reply messages will simply be discarded)
	//   2. we don't need to handle message send failures
	//      (by not setting a handler, failed messages will simply be discarded)
	//   3. we don't need to know when the channel is destroyed, so we don't need a destructor

	// we do, however, need to know if/when our peer dies (so we can remove it from the loop)
	eve_channel_set_peer_close_handler(incoming_proxy->channel, incoming_proxy_peer_close_handler);

	status = eve_loop_add_item(incoming_proxy->loop, incoming_proxy->channel);
	if (status != ferr_ok) {
		goto out;
	}

out:
	if (status == ferr_ok) {
		*out_proxy = (void*)incoming_proxy;
	} else if (incoming_proxy) {
		spooky_release((void*)incoming_proxy);
	}
	return status;
};

void* spooky_proxy_context(spooky_proxy_t* obj) {
	spooky_outgoing_proxy_object_t* outgoing_proxy = (void*)obj;
	if (spooky_proxy_is_incoming(obj)) {
		return NULL;
	}
	return outgoing_proxy->context;
};

// most of this was copied from `=spooky_interface_handle()
// TODO: make this DRY by sharing code with spooky_interface_handle()
static ferr_t spooky_outgoing_proxy_handle(spooky_proxy_t* obj, sys_channel_message_t* message, eve_channel_t* channel) {
	ferr_t status = ferr_ok;
	spooky_outgoing_proxy_object_t* proxy = (void*)obj;
	spooky_proxy_interface_object_t* interface = proxy->interface;
	spooky_invocation_t* invocation = NULL;
	spooky_deserializer_t deserializer;
	size_t name_length = 0;
	const char* name = NULL;
	size_t name_offset = 0;
	spooky_proxy_interface_entry_t* entry = NULL;

	status = spooky_deserializer_init(&deserializer, message);
	if (status != ferr_ok) {
		goto out;
	}

	status = spooky_deserializer_decode_integer(&deserializer, UINT64_MAX, NULL, &name_length, sizeof(name_length), NULL, false);
	if (status != ferr_ok) {
		goto out;
	}

	status = spooky_deserializer_skip(&deserializer, UINT64_MAX, &name_offset, name_length);
	if (status != ferr_ok) {
		goto out;
	}

	name = &deserializer.data[name_offset];

	status = ferr_no_such_resource;
	for (size_t i = 0; i < interface->entry_count; ++i) {
		entry = &interface->entries[i];

		if (entry->name_length != name_length) {
			continue;
		}

		if (simple_strncmp(entry->name, name, name_length) != 0) {
			continue;
		}

		status = ferr_ok;
		break;
	}

	if (status != ferr_ok) {
		entry = NULL;
		goto out;
	}

	// TODO: check that the types match

	status = spooky_invocation_create_incoming(channel, message, &invocation);
	if (status != ferr_ok) {
		goto out;
	}

	entry->implementation(proxy->context, invocation);

out:
	if (status == ferr_ok) {
		sys_release(message);
	} else {
		if (invocation) {
			spooky_release(invocation);
		}
	}
	return status;
};

static void spooky_outgoing_proxy_channel_destructor(void* context) {
	spooky_proxy_t* proxy = context;
	spooky_release(proxy);
};

static void spooky_outgoing_proxy_channel_message_handler(void* context, eve_channel_t* channel, sys_channel_message_t* message) {
	spooky_proxy_t* proxy = context;

	if (spooky_outgoing_proxy_handle(proxy, message, channel) != ferr_ok) {
		// just discard the message
		sys_console_log_f("Discarding message\n");
		sys_release(message);
	}

	// on success, `spooky_outgoing_proxy_handle` consumes the message
};

static void spooky_outgoing_proxy_channel_peer_close_handler(void* context, eve_channel_t* channel) {
	spooky_proxy_t* proxy = context;
	// by removing the channel from the loop, we remove the only reference to the channel,
	// causing it to close the underlying sys_channel and destroy the libeve channel (which calls our destructor and releases the proxy)
	LIBSPOOKY_WUR_IGNORE(eve_loop_remove_item(eve_loop_get_current(), channel));
};

ferr_t spooky_outgoing_proxy_create_channel(spooky_proxy_t* outgoing_proxy, sys_channel_t** out_sys_channel) {
	ferr_t status = ferr_ok;
	sys_channel_t* our_side = NULL;
	sys_channel_t* their_side = NULL;
	eve_channel_t* channel = NULL;

	status = spooky_retain(outgoing_proxy);
	if (status != ferr_ok) {
		outgoing_proxy = NULL;
		goto out;
	}

	status = sys_channel_create_pair(&our_side, &their_side);
	if (status != ferr_ok) {
		goto out;
	}

	status = eve_channel_create(our_side, outgoing_proxy, &channel);
	if (status != ferr_ok) {
		goto out;
	}

	// the channel now holds the only reference to our side of the sys_channel
	sys_release(our_side);
	our_side = NULL;

	eve_item_set_destructor(channel, spooky_outgoing_proxy_channel_destructor);
	eve_channel_set_message_handler(channel, spooky_outgoing_proxy_channel_message_handler);
	eve_channel_set_peer_close_handler(channel, spooky_outgoing_proxy_channel_peer_close_handler);
	// we discard messages that we failed to send, so don't set a message-send-failure handler

	// the channel destructor will release the outgoing proxy reference
	outgoing_proxy = NULL;

	status = eve_loop_add_item(eve_loop_get_main(), channel);
	if (status != ferr_ok) {
		goto out;
	}

	// the channel is kept alive by the loop until our peer closes their end, after which it closes our channel and releases the proxy
	eve_release(channel);

out:
	if (status == ferr_ok) {
		*out_sys_channel = their_side;
	} else {
		if (channel) {
			eve_release(channel);
		}
		if (our_side) {
			sys_release(our_side);
		}
		if (their_side) {
			sys_release(their_side);
		}
		if (outgoing_proxy) {
			spooky_release(outgoing_proxy);
		}
	}
	return status;
};
