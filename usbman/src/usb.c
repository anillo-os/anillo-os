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

#include <usbman/usb.private.h>
#include <libsimple/libsimple.h>
#include <ferro/bits.h>

#include <usbman/hid.h>
#include <libeve/libeve.h>

static usbman_device_object_t* device_list_head = NULL;
static sys_mutex_t device_list_lock = SYS_MUTEX_INIT;

static const usbman_interface_class_methods_t* interface_class_methods[256] = {0};
static sys_mutex_t interface_class_methods_mutex = SYS_MUTEX_INIT;

USBMAN_STRUCT(usbman_device_callback_context) {
	sys_semaphore_t semaphore;
	ferr_t status;
};

USBMAN_STRUCT(usbman_device_perform_transfer_callback_context) {
	usbman_device_callback_context_t common;
	uint16_t transferred;
};

static void usbman_device_make_request_callback(void* ctx, usbman_request_status_t status) {
	usbman_device_callback_context_t* context = ctx;
	context->status = (status == usbman_request_status_ok) ? ferr_ok : ferr_unknown;
	sys_semaphore_up(&context->semaphore);
};

static void usbman_device_configure_endpoint_callback(void* ctx, ferr_t status) {
	usbman_device_callback_context_t* context = ctx;
	context->status = status;
	sys_semaphore_up(&context->semaphore);
};

static void usbman_device_perform_transfer_callback(void* ctx, ferr_t status, uint16_t transferred) {
	usbman_device_perform_transfer_callback_context_t* context = ctx;
	context->common.status = status;
	context->transferred = transferred;
	sys_semaphore_up(&context->common.semaphore);
};

ferr_t usbman_device_make_request_blocking(usbman_device_object_t* device, usbman_request_direction_t direction, usbman_request_type_t type, usbman_request_recipient_t recipient, usbman_request_code_t code, uint16_t value, uint16_t index, void* physical_data, uint16_t data_length) {
	usbman_device_callback_context_t context = {
		.status = ferr_ok,
	};

	sys_semaphore_init(&context.semaphore, 0);

	context.status = device->methods->make_request((void*)device, direction, type, recipient, code, value, index, physical_data, data_length, usbman_device_make_request_callback, &context);
	if (context.status != ferr_ok) {
		goto out;
	}

	eve_semaphore_down(&context.semaphore);

out:
	return context.status;
};

static ferr_t usbman_device_configure_endpoints_blocking(usbman_device_object_t* device, const usbman_device_configure_endpoint_entry_t* entries, size_t entry_count) {
	usbman_device_callback_context_t context = {
		.status = ferr_ok,
	};

	sys_semaphore_init(&context.semaphore, 0);

	context.status = device->methods->configure_endpoints((void*)device, entries, entry_count, usbman_device_configure_endpoint_callback, &context);
	if (context.status != ferr_ok) {
		goto out;
	}

	eve_semaphore_down(&context.semaphore);

out:
	return context.status;
};

static ferr_t usbman_device_perform_transfer_blocking(usbman_device_object_t* device, uint8_t endpoint_number, usbman_endpoint_direction_t direction, void* physical_data, uint16_t data_length, uint16_t* out_transferred) {
	usbman_device_perform_transfer_callback_context_t context = {
		.common.status = ferr_ok,
	};

	sys_semaphore_init(&context.common.semaphore, 0);

	context.common.status = device->methods->perform_transfer((void*)device, endpoint_number, direction, physical_data, data_length, usbman_device_perform_transfer_callback, &context);
	if (context.common.status != ferr_ok) {
		goto out;
	}

	eve_semaphore_down(&context.common.semaphore);

out:
	if (context.common.status == ferr_ok) {
		if (out_transferred) {
			*out_transferred = context.transferred;
		}
	}
	return context.common.status;
};

void usbman_usb_init(void) {
	usbman_hid_init();
};

static void usbman_device_destroy(usbman_object_t* obj) {
	usbman_device_object_t* device = (void*)obj;
	sys_object_destroy(obj);
};

static const usbman_object_class_t usbman_device_object_class = {
	LIBSYS_OBJECT_CLASS_INTERFACE(NULL),
	.destroy = usbman_device_destroy,
};

const usbman_object_class_t* usbman_object_class_device(void) {
	return &usbman_device_object_class;
};

ferr_t usbman_device_lookup(uint16_t vendor_id, uint16_t product_id, usbman_device_t** out_device) {
	ferr_t status = ferr_no_such_resource;
	usbman_device_object_t* device = NULL;

	eve_mutex_lock(&device_list_lock);

	for (usbman_device_object_t* device = device_list_head; device != NULL; device = device->next) {
		if (device->vendor_id != vendor_id || device->product_id != product_id) {
			continue;
		}

		if (out_device) {
			if (usbman_retain((void*)device) != ferr_ok) {
				continue;
			}

			*out_device = (void*)device;
		}

		status = ferr_ok;

		break;
	}

out:
	sys_mutex_unlock(&device_list_lock);
out_unlocked:
	return status;
};

ferr_t usbman_controller_new(const usbman_controller_methods_t* methods, void* private_data, usbman_controller_t** out_controller) {
	ferr_t status = ferr_ok;
	usbman_controller_t* controller = NULL;

	if (!out_controller) {
		status = ferr_invalid_argument;
		goto out;
	}

	status = sys_mempool_allocate(sizeof(*controller), NULL, (void*)&controller);
	if (status != ferr_ok) {
		goto out;
	}

	simple_memset(controller, 0, sizeof(*controller));

	controller->private_data = private_data;
	controller->methods = methods;

out:
	if (status == ferr_ok) {
		*out_controller = controller;
	} else {
		if (controller) {
			USBMAN_WUR_IGNORE(sys_mempool_free(controller));
		}
	}
	return status;
};

ferr_t usbman_device_new(usbman_controller_t* controller, const usbman_device_methods_t* methods, void* private_data, usbman_device_object_t** out_device) {
	ferr_t status = ferr_ok;
	usbman_device_object_t* device = NULL;

	if (!out_device) {
		status = ferr_invalid_argument;
		goto out;
	}

	status = usbman_object_new(&usbman_device_object_class, sizeof(*device) - sizeof(device->object), (void*)&device);
	if (status != ferr_ok) {
		goto out;
	}

	device->private_data = private_data;
	device->controller = controller;
	device->methods = methods;

out:
	if (status == ferr_ok) {
		*out_device = device;
	} else {
		if (device) {
			USBMAN_WUR_IGNORE(usbman_release((void*)device));
		}
	}
	return status;
};

ferr_t usbman_device_publish(usbman_device_object_t* device) {
	ferr_t status = ferr_ok;
	usbman_device_t** device_pointer = NULL;
	bool created = false;
	bool release_on_fail = false;

	status = usbman_retain((void*)device);
	if (status != ferr_ok) {
		goto out_unlocked;
	}

	release_on_fail = true;

	eve_mutex_lock(&device_list_lock);

	device->next = device_list_head;
	device->prev = &device_list_head;

	if (device->next) {
		device->next->prev = &device->next;
	}

out:
	sys_mutex_unlock(&device_list_lock);
out_unlocked:
	if (status != ferr_ok) {
		if (release_on_fail) {
			usbman_release((void*)device);
		}
	}
	return status;
};

ferr_t usbman_device_unpublish(usbman_device_object_t* device) {
	ferr_t status = ferr_ok;
	usbman_device_t** device_pointer = NULL;

	eve_mutex_lock(&device_list_lock);

	if (device->next) {
		device->next->prev = device->prev;
	}
	*device->prev = device->next;

	device->next = NULL;
	device->prev = NULL;

	usbman_release((void*)device);

out:
	sys_mutex_unlock(&device_list_lock);
out_unlocked:
	return status;
};

static void usbman_device_finish_setup(usbman_device_object_t* device) {
	usbman_device_configure_endpoint_entry_t* entries = NULL;
	size_t entry_count = 0;

	sys_console_log("USB: finishing device setup\n");

#if 0
	for (size_t i = 0; i < device->configuration_count; ++i) {
		const usbman_configuration_t* config = &device->configurations[i];

		sys_console_log_f("USB: config #%u\n", config->id);

		for (size_t j = 0; j < config->interface_count; ++j) {
			const usbman_interface_t* interface = &config->interfaces[j];

			sys_console_log_f("USB: interface #%u\n", interface->id);

			for (size_t k = 0; k < interface->setting_count; ++k) {
				const usbman_interface_setting_t* setting = &interface->settings[k];

				sys_console_log_f(
					"USB: setting #%u:\n"
					"class=%u, subclass=%u\n"
					"protocol=%u, methods=%p\n"
					,
					setting->id,
					setting->interface_class, setting->interface_subclass,
					setting->interface_protocol, setting->interface_class_methods
				);

				for (size_t l = 0; l < setting->endpoint_count; ++l) {
					const usbman_endpoint_t* endpoint = &setting->endpoints[l];

					sys_console_log_f(
						"USB: endpoint #%u (%s):\n"
						"type=%u, usage_type=%u\n"
						"sync_type=%u, interval_power=%u\n"
						,
						endpoint->number, (endpoint->direction == usbman_endpoint_direction_in) ? "in" : "out",
						endpoint->type, endpoint->usage_type,
						endpoint->synchronization_type, endpoint->interval_power
					);
				}
			}
		}
	}
#endif

	if (device->configuration_count == 0) {
		return;
	}

	// just use the default configuration and interface settings

	device->active_configuration = device->configurations[0];

	for (size_t i = 0; i < device->active_configuration->interface_count; ++i) {
		usbman_interface_t* interface = device->active_configuration->interfaces[i];

		if (interface->setting_count == 0) {
			// what?! this shouldn't happen.
			continue;
		}

		interface->active_setting = interface->settings[0];

		for (size_t j = 0; j < interface->active_setting->endpoint_count; ++j) {
			usbman_endpoint_t* endpoint = interface->active_setting->endpoints[j];
			usbman_device_configure_endpoint_entry_t* entry = NULL;

			if (sys_mempool_reallocate(entries, sizeof(*entries) * (entry_count + 1), NULL, (void*)&entries) != ferr_ok) {
				sys_console_log("USB: failed to grow entry array\n");
				goto out;
			}

			++entry_count;

			entry = &entries[entry_count - 1];

			simple_memset(entry, 0, sizeof(*entry));

			entry->endpoint_number = endpoint->number;
			entry->direction = endpoint->direction;
			entry->interval_power = endpoint->interval_power;
			entry->max_packet_size = endpoint->max_packet_size;
			entry->type = endpoint->type;
		}
	}

	if (usbman_device_configure_endpoints_blocking(device, entries, entry_count) != ferr_ok) {
		sys_console_log("USB: failed to configure endpoints\n");
		goto out;
	}

	if (usbman_device_make_request_blocking(device, usbman_request_direction_host_to_device, usbman_request_type_standard, usbman_request_recipient_device, usbman_request_code_set_configuration, device->active_configuration->id, 0, NULL, 0) != ferr_ok) {
		sys_console_log("USB: failed to configure device\n");
		goto out;
	}

	sys_console_log_f("USB: V%04x:P%04x(%p): finished device setup\n", device->vendor_id, device->product_id, device);

	for (size_t i = 0; i < device->active_configuration->interface_count; ++i) {
		usbman_interface_t* interface = device->active_configuration->interfaces[i];

		if (interface->active_setting && interface->active_setting->interface_class_methods && interface->active_setting->interface_class_methods->setup_interface) {
			sys_console_log_f("USB: V%04x:P%04x(%p): handing interface %zu to class subsystem for additional setup\n", device->vendor_id, device->product_id, device, i);
			interface->active_setting->interface_class_methods->setup_interface(interface);
		}
	}

out:
	if (entries) {
		USBMAN_WUR_IGNORE(sys_mempool_free(entries));
		entries = NULL;
	}
};

USBMAN_ALWAYS_INLINE uint8_t round_down_to_alignment_power(uint64_t byte_count) {
	if (byte_count == 0) {
		return 0;
	}
	return ferro_bits_in_use_u64(byte_count) - 1;
};

USBMAN_ALWAYS_INLINE uint8_t round_up_to_alignment_power(uint64_t byte_count) {
	uint8_t power = round_down_to_alignment_power(byte_count);
	return ((1ull << power) < byte_count) ? (power + 1) : power;
};

static void usbman_device_setup_config(usbman_device_object_t* device, size_t config_id) {
	ferr_t status = ferr_ok;
	usbman_configuration_descriptor_t* desc = NULL;
	void* physical_temp = NULL;
	uint16_t total_length = 0;
	usbman_configuration_t* config = NULL;
	usbman_interface_t* curr_interface = NULL;
	usbman_interface_setting_t* curr_setting = NULL;
	usbman_endpoint_t* curr_endpoint = NULL;
	const usbman_descriptor_header_t* desc_space_end = NULL;

	status = sys_mempool_allocate_advanced(sizeof(usbman_configuration_descriptor_t), 0, round_up_to_alignment_power(64 * 1024), sys_mempool_flag_physically_contiguous, NULL, (void*)&desc);
	if (status != ferr_ok) {
		goto out;
	}

	status = sys_page_translate(desc, (void*)&physical_temp);
	if (status != ferr_ok) {
		goto out;
	}

	status = usbman_device_make_request_blocking(device, usbman_request_direction_device_to_host, usbman_request_type_standard, usbman_request_recipient_device, usbman_request_code_get_descriptor, (uint16_t)usbman_descriptor_type_configuration << 8 | config_id, 0, physical_temp, sizeof(usbman_configuration_descriptor_t));
	if (status != ferr_ok) {
		sys_console_log("USB: failed to get config descriptor\n");
		goto out;
	}

	total_length = desc->total_length;

	USBMAN_WUR_IGNORE(sys_mempool_free(desc));
	desc = NULL;

	// now let's fetch the entire descriptor, including all interfaces and endpoints

	status = sys_mempool_allocate_advanced(total_length, 0, round_up_to_alignment_power(64 * 1024), sys_mempool_flag_physically_contiguous, NULL, (void*)&desc);
	if (status != ferr_ok) {
		goto out;
	}

	status = sys_page_translate(desc, (void*)&physical_temp);
	if (status != ferr_ok) {
		goto out;
	}

	status = usbman_device_make_request_blocking(device, usbman_request_direction_device_to_host, usbman_request_type_standard, usbman_request_recipient_device, usbman_request_code_get_descriptor, (uint16_t)usbman_descriptor_type_configuration << 8 | config_id, 0, physical_temp, total_length);
	if (status != ferr_ok) {
		sys_console_log("USB: failed to get entire config descriptor\n");
		goto out;
	}

	status = sys_mempool_reallocate(device->configurations, sizeof(*device->configurations) * (device->configuration_count + 1), NULL, (void*)&device->configurations);
	if (status != ferr_ok) {
		goto out;
	}

	device->configurations[device->configuration_count] = NULL;

	desc_space_end = (const void*)((const char*)desc + desc->total_length);

	status = sys_mempool_allocate(sizeof(*config), NULL, (void*)&config);
	if (status != ferr_ok) {
		goto out;
	}

	++device->configuration_count;

	device->configurations[device->configuration_count - 1] = config;

	simple_memset(config, 0, sizeof(*config));

	config->id = desc->configuration_value;
	config->device = device;

	simple_memset(config->interfaces, 0, sizeof(*config->interfaces) * config->interface_count);

	for (usbman_descriptor_header_t* desc_header = (void*)((char*)desc + desc->header.length); desc_header < desc_space_end; desc_header = (void*)((char*)desc_header + desc_header->length)) {
		if (desc_header->length == 0) {
			break;
		}

		if (desc_header->descriptor_type == usbman_descriptor_type_interface) {
			usbman_interface_descriptor_t* interface_desc = (void*)desc_header;

			if (interface_desc->interface_number >= config->interface_count) {
				// NOTE: we assume that interface numbers are contiguous.
				//       this is a reasonable assumption, but i'm not sure if this is required by the spec.

				size_t old_count = config->interface_count;

				status = sys_mempool_reallocate(config->interfaces, sizeof(*config->interfaces) * (interface_desc->interface_number + 1), NULL, (void*)&config->interfaces);
				if (status != ferr_ok) {
					goto out;
				}

				simple_memset(&config->interfaces[old_count], 0, sizeof(*config->interfaces) * ((interface_desc->interface_number + 1) - old_count));

				status = sys_mempool_allocate(sizeof(**config->interfaces), NULL, (void*)&config->interfaces[interface_desc->interface_number]);
				if (status != ferr_ok) {
					goto out;
				}

				config->interface_count = interface_desc->interface_number + 1;

				simple_memset(config->interfaces[interface_desc->interface_number], 0, sizeof(**config->interfaces));
			}

			curr_interface = config->interfaces[interface_desc->interface_number];

			curr_interface->configuration = config;
			curr_interface->id = interface_desc->interface_number;

			if (interface_desc->alternate_setting >= curr_interface->setting_count) {
				// NOTE: same here, we assume setting numbers are contiguous.

				size_t old_count = curr_interface->setting_count;

				status = sys_mempool_reallocate(curr_interface->settings, sizeof(*curr_interface->settings) * (interface_desc->alternate_setting + 1), NULL, (void*)&curr_interface->settings);
				if (status != ferr_ok) {
					goto out;
				}

				simple_memset(&curr_interface->settings[old_count], 0, sizeof(*curr_interface->settings) * ((interface_desc->alternate_setting + 1) - old_count));

				status = sys_mempool_allocate(sizeof(**curr_interface->settings), NULL, (void*)&curr_interface->settings[interface_desc->alternate_setting]);
				if (status != ferr_ok) {
					goto out;
				}

				curr_interface->setting_count = interface_desc->alternate_setting + 1;

				simple_memset(curr_interface->settings[interface_desc->alternate_setting], 0, sizeof(**curr_interface->settings));
			}

			curr_setting = curr_interface->settings[interface_desc->alternate_setting];

			curr_setting->interface = curr_interface;
			curr_setting->id = interface_desc->alternate_setting;
			curr_setting->interface_class = interface_desc->interface_class;
			curr_setting->interface_subclass = interface_desc->interface_subclass;
			curr_setting->interface_protocol = interface_desc->interface_protocol;

			eve_mutex_lock(&interface_class_methods_mutex);
			curr_setting->interface_class_methods = interface_class_methods[curr_setting->interface_class];
			sys_mutex_unlock(&interface_class_methods_mutex);

			curr_endpoint = NULL;
		} else if (desc_header->descriptor_type == usbman_descriptor_type_endpoint) {
			usbman_endpoint_descriptor_t* endpoint_desc = (void*)desc_header;
			usbman_speed_id_t speed_id;
			uint64_t speed_in_125us = 0;

			if (!curr_setting) {
				sys_console_log("USB: found endpoint descriptor not associated with an interface?\n");
				continue;
			}

			status = sys_mempool_reallocate(curr_setting->endpoints, sizeof(*curr_setting->endpoints) * (curr_setting->endpoint_count + 1), NULL, (void*)&curr_setting->endpoints);
			if (status != ferr_ok) {
				goto out;
			}

			curr_setting->endpoints[curr_setting->endpoint_count] = NULL;

			status = sys_mempool_allocate(sizeof(*curr_endpoint), NULL, (void*)&curr_setting->endpoints[curr_setting->endpoint_count]);
			if (status != ferr_ok) {
				goto out;
			}

			++curr_setting->endpoint_count;

			curr_endpoint = curr_setting->endpoints[curr_setting->endpoint_count - 1];

			simple_memset(curr_endpoint, 0, sizeof(*curr_endpoint));

			curr_endpoint->setting = curr_setting;
			curr_endpoint->number = endpoint_desc->endpoint_address & 0x0f;
			curr_endpoint->direction = ((endpoint_desc->endpoint_address & (1 << 7)) != 0) ? usbman_endpoint_direction_in : usbman_endpoint_direction_out;
			curr_endpoint->type = endpoint_desc->attributes & 3;
			curr_endpoint->max_packet_size = endpoint_desc->max_packet_size;

			if (curr_endpoint->type == usbman_endpoint_type_interrupt || curr_endpoint->type == usbman_endpoint_type_isochronous) {
				curr_endpoint->usage_type = (endpoint_desc->attributes >> 4) & 3;
			}

			if (curr_endpoint->type == usbman_endpoint_type_isochronous) {
				curr_endpoint->synchronization_type = (endpoint_desc->attributes >> 2) & 3;
			}

			speed_id = device->methods->get_standard_speed((void*)device);

			switch (curr_endpoint->type) {
				case usbman_endpoint_type_control:
					// this shouldn't happen
					speed_in_125us = 1;
					break;
				case usbman_endpoint_type_isochronous:
					speed_in_125us = 1 << (endpoint_desc->interval - 1);

					// lower speed devices operate on 1ms rather than 125us
					if (speed_id == usbman_speed_id_full_speed || speed_id == usbman_speed_id_low_speed) {
						speed_in_125us *= 8;
					}
					break;
				case usbman_endpoint_type_bulk:
					// this case doesn't actually use a time period
					break;
				case usbman_endpoint_type_interrupt:
					if (speed_id == usbman_speed_id_full_speed || speed_id == usbman_speed_id_low_speed) {
						speed_in_125us = endpoint_desc->interval * 8;
					} else {
						speed_in_125us = 1 << (endpoint_desc->interval - 1);
					}
					break;
			}

			if (curr_endpoint->type == usbman_endpoint_type_bulk) {
				// use the interval value directly; this actually specifies the maximum NAK rate
				curr_endpoint->interval_power = endpoint_desc->interval;
			} else {
				// this implicitly rounds any speed values down to the nearest power of 2
				// (i.e. for low- and full-speed interrupt endpoints)
				curr_endpoint->interval_power = ferro_bits_in_use_u64(speed_in_125us) - 1;
			}
		} else {
			if (curr_setting && !curr_endpoint && curr_setting->interface_class_methods) {
				ferr_t status2 = curr_setting->interface_class_methods->process_descriptor(curr_setting, desc_header, &curr_setting->interface_class_private_data);

				if (status2 != ferr_ok) {
					sys_console_log_f("USB: %s (length=%u, type=%02x)\n", (status2 == ferr_invalid_argument) ? "ignoring unknown descriptor" : "error processing descriptor", desc_header->length, desc_header->descriptor_type);
				}
			} else {
				sys_console_log_f("USB: ignoring unknown descriptor (length=%u, type=%02x)\n", desc_header->length, desc_header->descriptor_type);
			}
		}
	}

out:
	if (status != ferr_ok) {
		sys_console_log("USB: failed to retrieve config header\n");
	}

	if (desc) {
		USBMAN_WUR_IGNORE(sys_mempool_free(desc));
		desc = NULL;
	}
};

static void usbman_device_setup_thread(void* context, sys_thread_t* this_thread) {
	usbman_device_object_t* device = context;
	void* physical_temp = NULL;
	ferr_t status = ferr_ok;
	usbman_device_descriptor_t* desc = NULL;
	size_t config_count = 0;

	status = sys_mempool_allocate_advanced(sizeof(*desc), 0, round_up_to_alignment_power(64 * 1024), sys_mempool_flag_physically_contiguous, NULL, (void*)&desc);
	if (status != ferr_ok) {
		goto out;
	}

	status = sys_page_translate(desc, (void*)&physical_temp);
	if (status != ferr_ok) {
		goto out;
	}

	status = usbman_device_make_request_blocking(device, usbman_request_direction_device_to_host, usbman_request_type_standard, usbman_request_recipient_device, usbman_request_code_get_descriptor, (uint16_t)usbman_descriptor_type_device << 8 | 0, 0, physical_temp, sizeof(usbman_device_descriptor_t));
	if (status != ferr_ok) {
		goto out;
	}

	if (status != ferr_ok) {
		sys_console_log("USB: failed to get device descriptor\n");
		goto out;
	}

	sys_console_log_f(
		"USB: device descriptor:\n"
		"length=%u, type=%u,\n"
		"version=%04x, class=%u\n"
		"subclass=%u, protocol=%u\n"
		"max_packet_size=%u, vendor_id=%04x\n"
		"product_id=%04x, device_version=%04x\n"
		"manufacturer_index=%u, product_index=%u\n"
		"serial_number_index=%u, configuration_count=%u\n"
		,
		desc->header.length,
		desc->header.descriptor_type,
		desc->usb_version,
		desc->device_class,
		desc->device_subclass,
		desc->device_protocol,
		desc->endpoint_0_max_packet_size,
		desc->vendor_id,
		desc->product_id,
		desc->device_version,
		desc->manufacturer_string_index,
		desc->product_string_index,
		desc->serial_number_string_index,
		desc->configuration_count
	);

	device->vendor_id = desc->vendor_id;
	device->product_id = desc->product_id;

	config_count = desc->configuration_count;

	USBMAN_WUR_IGNORE(sys_mempool_free(desc));
	desc = NULL;

	for (size_t i = 0; i < config_count; ++i) {
		usbman_device_setup_config(device, i);
	}

	usbman_device_finish_setup(device);

out:
	if (desc) {
		USBMAN_WUR_IGNORE(sys_mempool_free(desc));
	}
};

void usbman_device_setup(usbman_device_object_t* device) {
	ferr_t status = ferr_ok;
	bool managed = false;

	status = sys_thread_create(NULL, 2ull * 1024 * 1024, usbman_device_setup_thread, device, sys_thread_flag_resume, NULL);

out:
	if (status != ferr_ok) {
		sys_console_log("USB: failed to setup device\n");
	}
};

ferr_t usbman_register_interface_class(uint8_t class_code, const usbman_interface_class_methods_t* methods) {
	ferr_t status = ferr_ok;

	eve_mutex_lock(&interface_class_methods_mutex);

	if (interface_class_methods[class_code] != NULL) {
		status = ferr_resource_unavailable;
		goto out;
	}

	interface_class_methods[class_code] = methods;

out:
	sys_mutex_unlock(&interface_class_methods_mutex);
	return status;
};

ferr_t usbman_endpoint_perform_transfer(usbman_endpoint_t* endpoint, void* physical_data, uint16_t length, usbman_endpoint_perform_transfer_callback_f callback, void* context) {
	return endpoint->setting->interface->configuration->device->methods->perform_transfer((void*)endpoint->setting->interface->configuration->device, endpoint->number, endpoint->direction, physical_data, length, callback, context);
};

ferr_t usbman_endpoint_perform_transfer_blocking(usbman_endpoint_t* endpoint, void* physical_data, uint16_t length, uint16_t* out_transferred) {
	return usbman_device_perform_transfer_blocking(endpoint->setting->interface->configuration->device, endpoint->number, endpoint->direction, physical_data, length, out_transferred);
};
