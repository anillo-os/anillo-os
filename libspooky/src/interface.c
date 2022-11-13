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

#include <libspooky/interface.private.h>
#include <libspooky/invocation.private.h>
#include <libspooky/deserializer.private.h>
#include <libsimple/libsimple.h>

static void spooky_interface_destroy(spooky_object_t* obj) {
	spooky_interface_object_t* interface = (void*)obj;

	if (interface->entries) {
		for (size_t i = 0; i < interface->entry_count; ++i) {
			interface->entries[i].implementation(interface->entries[i].context, NULL);
		}

		LIBSPOOKY_WUR_IGNORE(sys_mempool_free(interface->entries));
	}
};

static const spooky_object_class_t interface_class = {
	LIBSYS_OBJECT_CLASS_INTERFACE(NULL),
	.destroy = spooky_interface_destroy,
};

const spooky_object_class_t* spooky_object_class_interface(void) {
	return &interface_class;
};

ferr_t spooky_interface_create(const spooky_interface_entry_t* entries, size_t entry_count, spooky_interface_t** out_interface) {
	ferr_t status = ferr_ok;
	spooky_interface_object_t* interface = NULL;

	status = sys_object_new(&interface_class, sizeof(*interface) - sizeof(interface->object), (void*)&interface);
	if (status != ferr_ok) {
		goto out;
	}

	interface->entry_count = 0;
	interface->entries = NULL;

	status = sys_mempool_allocate(sizeof(*entries) * entry_count, NULL, (void*)&interface->entries);
	if (status != ferr_ok) {
		goto out;
	}

	interface->entry_count = entry_count;
	simple_memcpy(interface->entries, entries, sizeof(*entries) * entry_count);

out:
	if (status == ferr_ok) {
		*out_interface = (void*)interface;
	} else if (interface) {
		sys_release((void*)interface);
	}
	return status;
};

static void spooky_interface_channel_destructor(void* context) {
	spooky_release(context);
};

static void spooky_interface_message_handler(void* context, eve_channel_t* channel, sys_channel_message_t* message) {
	spooky_interface_t* interface = context;

	if (spooky_interface_handle(interface, message, channel) != ferr_ok) {
		// just discard the message
		sys_console_log_f("Discarding message\n");
		sys_release(message);
	}

	// on success, `spooky_interface_handle` consumes the message
};

static void spooky_interface_peer_close_handler(void* context, eve_channel_t* channel) {
	// this shouldn't fail
	sys_abort_status(eve_loop_remove_item(eve_loop_get_current(), channel));
};

static void spooky_interface_message_send_error_handler(void* context, eve_channel_t* channel, sys_channel_message_t* message, ferr_t error) {
	if (message) {
		sys_release(message);
	}
};

ferr_t spooky_interface_adopt(spooky_interface_t* obj, sys_channel_t* sys_channel, eve_loop_t* loop) {
	spooky_interface_object_t* interface = (void*)obj;
	ferr_t status = ferr_ok;
	eve_channel_t* channel = NULL;

	status = spooky_retain(obj);
	if (status != ferr_ok) {
		obj = NULL;
		goto out;
	}

	status = eve_channel_create(sys_channel, interface, &channel);
	if (status != ferr_ok) {
		goto out;
	}

	eve_item_set_destructor(channel, spooky_interface_channel_destructor);
	eve_channel_set_message_handler(channel, spooky_interface_message_handler);
	eve_channel_set_peer_close_handler(channel, spooky_interface_peer_close_handler);
	eve_channel_set_message_send_error_handler(channel, spooky_interface_message_send_error_handler);

	status = eve_loop_add_item(loop, channel);
	if (status != ferr_ok) {
		goto out;
	}

	// the libeve channel holds on to this...
	sys_release(sys_channel);
	// ... and the loop holds on to this
	eve_release(channel);

out:
	if (status != ferr_ok) {
		if (channel) {
			eve_release(channel);
		}
		if (obj) {
			spooky_release(obj);
		}
	}
	return status;
};

ferr_t spooky_interface_handle(spooky_interface_t* obj, sys_channel_message_t* message, eve_channel_t* channel) {
	ferr_t status = ferr_ok;
	spooky_interface_object_t* interface = (void*)obj;
	spooky_invocation_t* invocation = NULL;
	spooky_deserializer_t deserializer;
	size_t name_length = 0;
	const char* name = NULL;
	size_t name_offset = 0;
	spooky_interface_entry_t* entry = NULL;

	status = spooky_deserializer_init(&deserializer, sys_channel_message_data(message), sys_channel_message_length(message));
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

	entry->implementation(entry->context, invocation);

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
