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

#include <libpci/libpci.h>
#include <libpci/device.private.h>

// normally, this would have to be a mutex/rwlock instead because the connection might die and need to be reinitialized.
// however, pciman is a kernel-space manager, so it will not unexpectedly close our connection.
static sys_once_t pci_connection_init = SYS_ONCE_INITIALIZER;
static eve_channel_t* pci_connection = NULL;

static void main_channel_message_handler(void* context, eve_channel_t* channel, sys_channel_message_t* message) {
	// we discard non-reply messages on the main channel
	// (it's only used for querying the PCI tree)
	sys_release(message);
};

static void main_channel_peer_close_handler(void* context, eve_channel_t* channel) {
	LIBPCI_WUR_IGNORE(eve_loop_remove_item(eve_loop_get_current(), channel));
};

static void main_channel_message_send_error_handler(void* context, eve_channel_t* channel, sys_channel_message_t* message, ferr_t error) {
	sys_release(message);
};

static void do_pci_connection_init(void* context) {
	sys_channel_t* sys_channel = NULL;
	sys_abort_status_log(sys_channel_connect("org.anillo.pciman", sys_channel_realm_global, 0, &sys_channel));
	sys_abort_status_log(eve_channel_create(sys_channel, NULL, &pci_connection));
	sys_release(sys_channel);
	eve_channel_set_message_handler(pci_connection, main_channel_message_handler);
	eve_channel_set_peer_close_handler(pci_connection, main_channel_peer_close_handler);
	eve_channel_set_message_send_error_handler(pci_connection, main_channel_message_send_error_handler);
	sys_abort_status_log(eve_loop_add_item(eve_loop_get_main(), pci_connection));
	eve_release(pci_connection);
};

static void ensure_pci_connection(void) {
	sys_once(&pci_connection_init, do_pci_connection_init, NULL, 0);
};

ferr_t pci_visit(pci_visitor_f iterator, void* context) {
	sys_channel_message_t* message = NULL;
	sys_channel_message_t* reply = NULL;
	uint8_t* outgoing_message_data = NULL;
	ferr_t status = ferr_ok;
	sys_channel_conversation_id_t convo_id = sys_channel_conversation_id_none;
	const void* incoming_message_data = NULL;
	size_t info_count = 0;

	ensure_pci_connection();

	status = sys_channel_message_create(1, &message);
	if (status != ferr_ok) {
		goto out;
	}

	outgoing_message_data = sys_channel_message_data(message);
	outgoing_message_data[0] = 1;

	status = eve_channel_conversation_create(pci_connection, &convo_id);
	if (status != ferr_ok) {
		goto out;
	}

	sys_channel_message_set_conversation_id(message, convo_id);

	status = eve_channel_send_with_reply_sync(pci_connection, message, &reply);
	if (status != ferr_ok) {
		goto out;
	}

	// the message was consumed
	message = NULL;

	incoming_message_data = sys_channel_message_data(reply);

	if (sys_channel_message_length(reply) < sizeof(ferr_t)) {
		// bad reply
		status = ferr_should_restart;
		goto out;
	}

	status = *(ferr_t*)incoming_message_data;
	if (status != ferr_ok) {
		goto out;
	}

	info_count = (sys_channel_message_length(reply) - sizeof(ferr_t)) / sizeof(pci_device_info_t);

	for (size_t i = 0; i < info_count; ++i) {
		if (!iterator(context, incoming_message_data + sizeof(ferr_t) + (i * sizeof(pci_device_info_t)))) {
			status = ferr_cancelled;
			break;
		}
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

static void pci_device_destroy(pci_object_t* object) {
	pci_device_object_t* device = (void*)object;

	if (device->channel) {
		// in this case, we use the channel destructor to clean up the device object,
		// so all we do here is close our end and release our reference on the channel.
		// pciman will then close its end and we'll get notified that it did that and
		// remove the channel from the loop, resulting in the channel being released
		// and then destroyed.
		sys_channel_t* sys_channel = NULL;
		if (eve_channel_target(device->channel, false, &sys_channel) == ferr_ok) {
			LIBPCI_WUR_IGNORE(sys_channel_close(sys_channel));
		}
		eve_release(device->channel);
	} else {
		// in this case, we don't have a channel yet, so we clean up the device object ourselves
		LIBPCI_WUR_IGNORE(sys_mempool_free(device));
	}
};

static const pci_object_class_t pci_device_class = {
	LIBSYS_OBJECT_CLASS_INTERFACE(NULL),
	.destroy = pci_device_destroy,
};

const pci_object_class_t* pci_object_class_device(void) {
	return &pci_device_class;
};

static void device_message_handler(void* context, eve_channel_t* channel, sys_channel_message_t* message) {
	pci_device_object_t* device = context;
	uint64_t data = *(uint64_t*)sys_channel_message_data(message);

	// the only messages we expected to receive that aren't replies are interrupt notifications.
	// discard the message itself and call the interrupt handler
	sys_release(message);

	if (device->interrupt_handler) {
		device->interrupt_handler(device->interrupt_handler_context, (void*)device, data);
	}
};

static void device_peer_close_handler(void* context, eve_channel_t* channel) {
	pci_device_object_t* device = context;
	LIBPCI_WUR_IGNORE(eve_loop_remove_item(eve_loop_get_current(), channel));
};

static void device_message_send_error_handler(void* context, eve_channel_t* channel, sys_channel_message_t* message, ferr_t error) {
	pci_device_object_t* device = context;
	sys_release(message);
};

static void device_channel_destructor(void* context) {
	pci_device_object_t* device = context;
	LIBPCI_WUR_IGNORE(sys_mempool_free(device));
};

LIBPCI_PACKED_STRUCT(pciman_device_registration_request) {
	uint8_t message_type;
	uint16_t vendor_id;
	uint16_t device_id;
};

ferr_t pci_connect(const pci_device_info_t* target, pci_device_t** out_device) {
	ferr_t status = ferr_ok;
	pci_device_object_t* device = NULL;
	sys_channel_t* sys_channel = NULL;
	sys_channel_message_t* request = NULL;
	pciman_device_registration_request_t* request_body = NULL;
	sys_channel_message_t* reply = NULL;
	sys_channel_conversation_id_t convo_id;

	status = sys_object_new(&pci_device_class, sizeof(*device) - sizeof(device->object), (void*)&device);
	if (status != ferr_ok) {
		goto out;
	}

	device->channel = NULL;
	device->interrupt_handler = NULL;
	device->interrupt_handler_context = NULL;

	status = sys_channel_connect("org.anillo.pciman", sys_channel_realm_global, 0, &sys_channel);
	if (status != ferr_ok) {
		goto out;
	}

	status = eve_channel_create(sys_channel, device, &device->channel);
	if (status != ferr_ok) {
		goto out;
	}

	// the channel now owns the only reference to the sys_channel
	sys_release(sys_channel);
	sys_channel = NULL;

	eve_channel_set_message_handler(device->channel, device_message_handler);
	eve_channel_set_peer_close_handler(device->channel, device_peer_close_handler);
	eve_channel_set_message_send_error_handler(device->channel, device_message_send_error_handler);
	eve_item_set_destructor(device->channel, device_channel_destructor);

	status = eve_loop_add_item(eve_loop_get_main(), device->channel);
	if (status != ferr_ok) {
		goto out;
	}

	status = sys_channel_message_create(sizeof(*request_body), &request);
	if (status != ferr_ok) {
		goto out;
	}

	status = eve_channel_conversation_create(device->channel, &convo_id);
	if (status != ferr_ok) {
		goto out;
	}

	sys_channel_message_set_conversation_id(request, convo_id);

	request_body = sys_channel_message_data(request);
	request_body->message_type = 2;
	request_body->vendor_id = target->vendor_id;
	request_body->device_id = target->device_id;

	status = eve_channel_send_with_reply_sync(device->channel, request, &reply);
	if (status != ferr_ok) {
		goto out;
	}

	// the message was consumed
	request = NULL;

	if (sys_channel_message_length(reply) != sizeof(ferr_t)) {
		// bad reply
		status = ferr_should_restart;
		goto out;
	}

	status = *(ferr_t*)sys_channel_message_data(reply);

out:
	if (reply) {
		sys_release(reply);
	}
	if (request) {
		sys_release(request);
	}
	if (status == ferr_ok) {
		FERRO_WUR_IGNORE(pci_retain((void*)device));
		*out_device = (void*)device;
	} else {
		if (sys_channel) {
			sys_release(sys_channel);
		}
		if (device) {
			pci_release((void*)device);
		}
	}
	return status;
};

ferr_t pci_device_register_interrupt_handler(pci_device_t* obj, pci_device_interrupt_handler_f interrupt_handler, void* context) {
	pci_device_object_t* device = (void*)obj;
	ferr_t status = ferr_ok;
	sys_channel_message_t* request = NULL;
	sys_channel_message_t* reply = NULL;
	sys_channel_conversation_id_t convo_id;

	device->interrupt_handler = interrupt_handler;
	device->interrupt_handler_context = context;

	status = sys_channel_message_create(1, &request);
	if (status != ferr_ok) {
		goto out;
	}

	status = eve_channel_conversation_create(device->channel, &convo_id);
	if (status != ferr_ok) {
		goto out;
	}

	sys_channel_message_set_conversation_id(request, convo_id);

	*(uint8_t*)sys_channel_message_data(request) = 3;

	status = eve_channel_send_with_reply_sync(device->channel, request, &reply);
	if (status != ferr_ok) {
		goto out;
	}

	// the message was consumed
	request = NULL;

	if (sys_channel_message_length(reply) != sizeof(ferr_t)) {
		// bad reply
		status = ferr_should_restart;
		goto out;
	}

	status = *(ferr_t*)sys_channel_message_data(reply);

out:
	if (reply) {
		sys_release(reply);
	}
	if (request) {
		sys_release(request);
	}
	return status;
};

LIBPCI_PACKED_STRUCT(pciman_read_on_interrupt_request) {
	uint8_t message_id;
	uint8_t bar_index;
	uint8_t size;
	uint64_t offset;
};

ferr_t pci_device_read_on_interrupt(pci_device_t* obj, uint8_t bar_index, uint64_t offset, uint8_t size) {
	pci_device_object_t* device = (void*)obj;
	ferr_t status = ferr_ok;
	sys_channel_message_t* request = NULL;
	pciman_read_on_interrupt_request_t* request_body = NULL;
	sys_channel_message_t* reply = NULL;
	sys_channel_conversation_id_t convo_id;

	status = sys_channel_message_create(sizeof(*request_body), &request);
	if (status != ferr_ok) {
		goto out;
	}

	status = eve_channel_conversation_create(device->channel, &convo_id);
	if (status != ferr_ok) {
		goto out;
	}

	sys_channel_message_set_conversation_id(request, convo_id);

	request_body = sys_channel_message_data(request);
	request_body->message_id = 8;
	request_body->bar_index = bar_index;
	request_body->offset = offset;
	request_body->size = size;

	status = eve_channel_send_with_reply_sync(device->channel, request, &reply);
	if (status != ferr_ok) {
		goto out;
	}

	// the message was consumed
	request = NULL;

	if (sys_channel_message_length(reply) != sizeof(ferr_t)) {
		// bad reply
		status = ferr_should_restart;
		goto out;
	}

	status = *(ferr_t*)sys_channel_message_data(reply);

out:
	if (reply) {
		sys_release(reply);
	}
	if (request) {
		sys_release(request);
	}
	return status;
};

LIBPCI_PACKED_STRUCT(pciman_write_on_interrupt_request) {
	uint8_t message_id;
	uint8_t bar_index;
	uint8_t size;
	uint64_t offset;
	uint64_t data;
};

ferr_t pci_device_write_on_interrupt(pci_device_t* obj, uint8_t bar_index, uint64_t offset, uint8_t size, uint64_t data) {
	pci_device_object_t* device = (void*)obj;
	ferr_t status = ferr_ok;
	sys_channel_message_t* request = NULL;
	pciman_write_on_interrupt_request_t* request_body = NULL;
	sys_channel_message_t* reply = NULL;
	sys_channel_conversation_id_t convo_id;

	status = sys_channel_message_create(sizeof(*request_body), &request);
	if (status != ferr_ok) {
		goto out;
	}

	status = eve_channel_conversation_create(device->channel, &convo_id);
	if (status != ferr_ok) {
		goto out;
	}

	sys_channel_message_set_conversation_id(request, convo_id);

	request_body = sys_channel_message_data(request);
	request_body->message_id = 9;
	request_body->bar_index = bar_index;
	request_body->offset = offset;
	request_body->size = size;
	request_body->data = data;

	status = eve_channel_send_with_reply_sync(device->channel, request, &reply);
	if (status != ferr_ok) {
		goto out;
	}

	// the message was consumed
	request = NULL;

	if (sys_channel_message_length(reply) != sizeof(ferr_t)) {
		// bad reply
		status = ferr_should_restart;
		goto out;
	}

	status = *(ferr_t*)sys_channel_message_data(reply);

out:
	if (reply) {
		sys_release(reply);
	}
	if (request) {
		sys_release(request);
	}
	return status;
};

LIBPCI_PACKED_STRUCT(pciman_get_mapped_bar_request) {
	uint8_t message_id;
	uint8_t bar_index;
};

ferr_t pci_device_get_mapped_bar(pci_device_t* obj, uint8_t bar_index, sys_shared_memory_t** out_bar, size_t* out_bar_size) {
	pci_device_object_t* device = (void*)obj;
	ferr_t status = ferr_ok;
	sys_channel_message_t* request = NULL;
	pciman_get_mapped_bar_request_t* request_body = NULL;
	sys_channel_message_t* reply = NULL;
	sys_channel_conversation_id_t convo_id;

	status = sys_channel_message_create(sizeof(*request_body), &request);
	if (status != ferr_ok) {
		goto out;
	}

	status = eve_channel_conversation_create(device->channel, &convo_id);
	if (status != ferr_ok) {
		goto out;
	}

	sys_channel_message_set_conversation_id(request, convo_id);

	request_body = sys_channel_message_data(request);
	request_body->message_id = 4;
	request_body->bar_index = bar_index;

	status = eve_channel_send_with_reply_sync(device->channel, request, &reply);
	if (status != ferr_ok) {
		goto out;
	}

	// the message was consumed
	request = NULL;

	if (sys_channel_message_length(reply) != sizeof(ferr_t) + sizeof(uint64_t)) {
		// bad reply
		status = ferr_should_restart;
		goto out;
	}

	status = *(ferr_t*)sys_channel_message_data(reply);
	if (status != ferr_ok) {
		goto out;
	}

	if (out_bar_size) {
		*out_bar_size = *(uint64_t*)(sys_channel_message_data(reply) + sizeof(ferr_t));
	}

	status = sys_channel_message_detach_shared_memory(reply, 0, out_bar);

out:
	if (reply) {
		sys_release(reply);
	}
	if (request) {
		sys_release(request);
	}
	return status;
};

ferr_t pci_device_enable_bus_mastering(pci_device_t* obj) {
	pci_device_object_t* device = (void*)obj;
	ferr_t status = ferr_ok;
	sys_channel_message_t* request = NULL;
	sys_channel_message_t* reply = NULL;
	sys_channel_conversation_id_t convo_id;

	status = sys_channel_message_create(1, &request);
	if (status != ferr_ok) {
		goto out;
	}

	status = eve_channel_conversation_create(device->channel, &convo_id);
	if (status != ferr_ok) {
		goto out;
	}

	sys_channel_message_set_conversation_id(request, convo_id);

	*(uint8_t*)sys_channel_message_data(request) = 5;

	status = eve_channel_send_with_reply_sync(device->channel, request, &reply);
	if (status != ferr_ok) {
		goto out;
	}

	// the message was consumed
	request = NULL;

	if (sys_channel_message_length(reply) != sizeof(ferr_t)) {
		// bad reply
		status = ferr_should_restart;
		goto out;
	}

	status = *(ferr_t*)sys_channel_message_data(reply);

out:
	if (reply) {
		sys_release(reply);
	}
	if (request) {
		sys_release(request);
	}
	return status;
};

LIBPCI_PACKED_STRUCT(pciman_config_space_read_request) {
	uint8_t message_type;
	uint64_t offset;
	uint8_t size;
};

ferr_t pci_device_config_space_read(pci_device_t* obj, size_t offset, uint8_t size, void* out_data) {
	pci_device_object_t* device = (void*)obj;
	ferr_t status = ferr_ok;
	sys_channel_message_t* request = NULL;
	pciman_config_space_read_request_t* request_body = NULL;
	sys_channel_message_t* reply = NULL;
	sys_channel_conversation_id_t convo_id;

	status = sys_channel_message_create(sizeof(*request_body), &request);
	if (status != ferr_ok) {
		goto out;
	}

	status = eve_channel_conversation_create(device->channel, &convo_id);
	if (status != ferr_ok) {
		goto out;
	}

	sys_channel_message_set_conversation_id(request, convo_id);

	request_body = sys_channel_message_data(request);
	request_body->message_type = 6;
	request_body->offset = offset;
	request_body->size = size;

	status = eve_channel_send_with_reply_sync(device->channel, request, &reply);
	if (status != ferr_ok) {
		goto out;
	}

	// the message was consumed
	request = NULL;

	if (sys_channel_message_length(reply) < sizeof(ferr_t)) {
		// bad reply
		status = ferr_should_restart;
		goto out;
	}

	status = *(ferr_t*)sys_channel_message_data(reply);
	if (status != ferr_ok) {
		goto out;
	}

	if (sys_channel_message_length(reply) != sizeof(ferr_t) + size) {
		// bad reply
		status = ferr_should_restart;
		goto out;
	}

	simple_memcpy(out_data, sys_channel_message_data(reply) + sizeof(ferr_t), size);

out:
	if (reply) {
		sys_release(reply);
	}
	if (request) {
		sys_release(request);
	}
	return status;
};

LIBPCI_PACKED_STRUCT(pciman_config_space_write_request) {
	uint8_t message_type;
	uint64_t offset;
	uint8_t size;
	char data[];
};

ferr_t pci_device_config_space_write(pci_device_t* obj, size_t offset, uint8_t size, const void* data) {
	pci_device_object_t* device = (void*)obj;
	ferr_t status = ferr_ok;
	sys_channel_message_t* request = NULL;
	pciman_config_space_write_request_t* request_body = NULL;
	sys_channel_message_t* reply = NULL;
	sys_channel_conversation_id_t convo_id;

	status = sys_channel_message_create(sizeof(*request_body) + size, &request);
	if (status != ferr_ok) {
		goto out;
	}

	status = eve_channel_conversation_create(device->channel, &convo_id);
	if (status != ferr_ok) {
		goto out;
	}

	sys_channel_message_set_conversation_id(request, convo_id);

	request_body = sys_channel_message_data(request);
	request_body->message_type = 7;
	request_body->offset = offset;
	request_body->size = size;
	simple_memcpy(request_body->data, data, size);

	status = eve_channel_send_with_reply_sync(device->channel, request, &reply);
	if (status != ferr_ok) {
		goto out;
	}

	// the message was consumed
	request = NULL;

	if (sys_channel_message_length(reply) != sizeof(ferr_t)) {
		// bad reply
		status = ferr_should_restart;
		goto out;
	}

	status = *(ferr_t*)sys_channel_message_data(reply);

out:
	if (reply) {
		sys_release(reply);
	}
	if (request) {
		sys_release(request);
	}
	return status;
};
