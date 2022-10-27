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

#ifndef _FERRO_DRIVERS_USB_PRIVATE_H_
#define _FERRO_DRIVERS_USB_PRIVATE_H_

#include <ferro/drivers/usb.h>
#include <ferro/core/refcount.h>

FERRO_DECLARATIONS_BEGIN;

FERRO_STRUCT_FWD(fusb_controller);
FERRO_STRUCT_FWD(fusb_device);

FERRO_ENUM(uint8_t, fusb_request_direction) {
	fusb_request_direction_host_to_device = 0,
	fusb_request_direction_device_to_host,
};

FERRO_ENUM(uint8_t, fusb_request_type) {
	fusb_request_type_standard = 0,
	fusb_request_type_class,
	fusb_request_type_vendor,
};

FERRO_ENUM(uint8_t, fusb_request_recipient) {
	fusb_request_recipient_device,
	fusb_request_recipient_interface,
	fusb_request_recipient_endpoint,
	fusb_request_recipient_other,

	fusb_request_recipient_vendor_specific = 31,
};

FERRO_ENUM(uint8_t, fusb_request_code) {
	//
	// USB2 and USB3
	//

	fusb_request_code_get_status = 0,
	fusb_request_code_clear_feature,

	fusb_request_code_set_feature = 3,

	fusb_request_code_set_address = 5,
	fusb_request_code_get_descriptor,
	fusb_request_code_set_descriptor,
	fusb_request_code_get_configuration,
	fusb_request_code_set_configuration,
	fusb_request_code_get_interface,
	fusb_request_code_set_interface,
	fusb_request_code_synch_frame,

	//
	// USB3
	//

	fusb_request_code_set_encryption,
	fusb_request_code_get_encryption,
	fusb_request_code_set_handshake,
	fusb_request_code_get_handshake,
	fusb_request_code_set_connection,
	fusb_request_code_set_security_data,
	fusb_request_code_get_security_data,
	fusb_request_code_set_wusb_data,
	fusb_request_code_loopback_data_write,
	fusb_request_code_loopback_data_read,
	fusb_request_code_set_interface_ds,

	fusb_request_code_set_sel = 48,
	fusb_request_code_set_isoch_delay,
};

FERRO_ENUM(uint8_t, fusb_descriptor_type) {
	//
	// USB2 and USB3
	//

	fusb_descriptor_type_device = 1,
	fusb_descriptor_type_configuration,
	fusb_descriptor_type_string,
	fusb_descriptor_type_interface,
	fusb_descriptor_type_endpoint,

	//
	// USB2 only, reserved in USB3
	//

	fusb_descriptor_type_device_qualifier,
	fusb_descriptor_type_other_speed_configuration,

	//
	// USB2 and USB3
	//

	fusb_descriptor_type_interface_power,

	//
	// USB3 only
	//

	fusb_descriptor_type_otg,
	fusb_descriptor_type_debug,
	fusb_descriptor_type_interface_association,

	fusb_descriptor_type_bos = 15,
	fusb_descriptor_type_device_capability,

	fusb_descriptor_type_superspeed_usb_endpoint_companion = 48,
	fusb_descriptor_type_superspeedplus_isochronous_endpoint_companion,
};

FERRO_ENUM(int, fusb_request_status) {
	fusb_request_status_ok = 0,
	fusb_request_status_unknown = -1,
};

FERRO_ENUM(uint8_t, fusb_endpoint_direction) {
	fusb_endpoint_direction_out = 0,
	fusb_endpoint_direction_in,
};

FERRO_ENUM(uint8_t, fusb_speed_id) {
	fusb_speed_id_invalid = 0,
	fusb_speed_id_full_speed,
	fusb_speed_id_low_speed,
	fusb_speed_id_high_speed,
	fusb_speed_id_super_speed_gen_1_x1,
	fusb_speed_id_super_speed_plus_gen_2_x1,
	fusb_speed_id_super_speed_plus_gen_1_x2,
	fusb_speed_id_super_speed_plus_gen_2_x2,
};

static const uint64_t fusb_maximum_bitrates[] = {
	0,
	12000000,
	1500000,
	480000000,
	5000000000,
	10000000000,
	5000000000,
	10000000000,
};

FERRO_ENUM(uint8_t, fusb_endpoint_type) {
	fusb_endpoint_type_control = 0,
	fusb_endpoint_type_isochronous,
	fusb_endpoint_type_bulk,
	fusb_endpoint_type_interrupt,
};

FERRO_STRUCT(fusb_device_configure_endpoint_entry) {
	uint8_t endpoint_number;
	fusb_endpoint_direction_t direction;
	uint8_t interval_power;
	uint16_t max_packet_size;
	fusb_endpoint_type_t type;
};

typedef void (*fusb_device_request_callback_f)(void* context, fusb_request_status_t status);
typedef void (*fusb_device_configure_endpoint_callback_f)(void* context, ferr_t status);
typedef void (*fusb_device_perform_transfer_callback_f)(void* context, ferr_t status, uint16_t transferred);

typedef ferr_t (*fusb_device_make_request_f)(fusb_device_t* device, fusb_request_direction_t direction, fusb_request_type_t type, fusb_request_recipient_t recipient, fusb_request_code_t code, uint16_t value, uint16_t index, void* physical_data, uint16_t data_length, fusb_device_request_callback_f callback, void* context);
typedef ferr_t (*fusb_device_configure_endpoints_f)(fusb_device_t* device, const fusb_device_configure_endpoint_entry_t* entries, size_t entry_count, fusb_device_configure_endpoint_callback_f callback, void* context);
typedef fusb_speed_id_t (*fusb_device_get_standard_speed_f)(fusb_device_t* device);
typedef ferr_t (*fusb_device_perform_transfer_f)(fusb_device_t* device, uint8_t endpoint_number, fusb_endpoint_direction_t direction, void* physical_data, uint16_t data_length, fusb_device_perform_transfer_callback_f callback, void* context);

FERRO_STRUCT(fusb_controller_methods) {};
FERRO_STRUCT(fusb_device_methods) {
	fusb_device_make_request_f make_request;
	fusb_device_configure_endpoints_f configure_endpoints;
	fusb_device_get_standard_speed_f get_standard_speed;
	fusb_device_perform_transfer_f perform_transfer;
};

FERRO_STRUCT(fusb_controller) {
	void* private_data;
	const fusb_controller_methods_t* methods;
};

FERRO_STRUCT_FWD(fusb_configuration);

FERRO_STRUCT(fusb_device) {
	void* private_data;
	fusb_controller_t* controller;
	const fusb_device_methods_t* methods;
	frefcount_t refcount;
	uint16_t vendor_id;
	uint16_t product_id;

	fusb_device_t* next;
	fusb_device_t** prev;

	fusb_configuration_t** configurations;
	size_t configuration_count;

	fusb_configuration_t* active_configuration;
};

FERRO_STRUCT(fusb_configuration) {
	fusb_device_t* device;

	fusb_interface_t** interfaces;
	size_t interface_count;

	uint8_t id;
};

FERRO_ENUM(uint8_t, fusb_endpoint_interrupt_usage_type) {
	fusb_endpoint_interrupt_usage_type_periodic,
	fusb_endpoint_interrupt_usage_type_notification,
};

FERRO_ENUM(uint8_t, fusb_endpoint_isochronous_usage_type) {
	fusb_endpoint_isochronous_usage_type_data,
	fusb_endpoint_isochronous_usage_type_feedback,
	fusb_endpoint_isochronous_usage_type_implicit_feedback_data,
};

FERRO_ENUM(uint8_t, fusb_endpoint_isochronous_syncronization_type) {
	fusb_endpoint_isochronous_syncronization_type_no_synchronization,
	fusb_endpoint_isochronous_syncronization_type_asynchronous,
	fusb_endpoint_isochronous_syncronization_type_adaptive,
	fusb_endpoint_isochronous_syncronization_type_synchronous,
};

FERRO_STRUCT_FWD(fusb_interface_setting);

FERRO_STRUCT(fusb_endpoint) {
	fusb_interface_setting_t* setting;

	uint8_t number;
	fusb_endpoint_direction_t direction;
	fusb_endpoint_type_t type;
	uint8_t usage_type;
	uint8_t synchronization_type;
	uint8_t interval_power;
	uint16_t max_packet_size;
};

FERRO_STRUCT_FWD(fusb_interface_class_methods);

FERRO_STRUCT(fusb_interface_setting) {
	fusb_interface_t* interface;

	fusb_endpoint_t** endpoints;
	size_t endpoint_count;

	uint8_t id;
	uint8_t interface_class;
	uint8_t interface_subclass;
	uint8_t interface_protocol;

	void* interface_class_private_data;

	const fusb_interface_class_methods_t* interface_class_methods;
};

FERRO_STRUCT(fusb_interface) {
	fusb_configuration_t* configuration;

	fusb_interface_setting_t** settings;
	size_t setting_count;

	uint8_t id;

	fusb_interface_setting_t* active_setting;
};

FERRO_PACKED_STRUCT(fusb_descriptor_header) {
	uint8_t length;
	uint8_t descriptor_type;
};

FERRO_PACKED_STRUCT(fusb_device_descriptor) {
	fusb_descriptor_header_t header;
	uint16_t usb_version;
	uint8_t device_class;
	uint8_t device_subclass;
	uint8_t device_protocol;

	/**
	 * @note The meaning of the value of this field changes depending on the #usb_version field.
	 *       For USB 3.0, this field is an exponent of two indicating the maximum packet size.
	 *       For USB 2.0, this field is an exact number of bytes indicating the maximum packet size.
	 */
	uint8_t endpoint_0_max_packet_size;

	uint16_t vendor_id;
	uint16_t product_id;
	uint16_t device_version;
	uint8_t manufacturer_string_index;
	uint8_t product_string_index;
	uint8_t serial_number_string_index;
	uint8_t configuration_count;
};

FERRO_PACKED_STRUCT(fusb_configuration_descriptor) {
	fusb_descriptor_header_t header;
	uint16_t total_length;
	uint8_t interface_count;
	uint8_t configuration_value;
	uint8_t configuration_string_index;
	uint8_t attributes;
	uint8_t max_power;
};

FERRO_PACKED_STRUCT(fusb_interface_descriptor) {
	fusb_descriptor_header_t header;
	uint8_t interface_number;
	uint8_t alternate_setting;
	uint8_t endpoint_count;
	uint8_t interface_class;
	uint8_t interface_subclass;
	uint8_t interface_protocol;
	uint8_t interface_string_index;
};

FERRO_PACKED_STRUCT(fusb_endpoint_descriptor) {
	fusb_descriptor_header_t header;
	uint8_t endpoint_address;
	uint8_t attributes;
	uint16_t max_packet_size;
	uint8_t interval;
};

FERRO_PACKED_STRUCT(fusb_string_descriptor) {
	fusb_descriptor_header_t header;
	uint16_t content[];
};

typedef ferr_t (*fusb_interface_class_process_descriptor_f)(fusb_interface_setting_t* interface_setting, const fusb_descriptor_header_t* descriptor, void** in_out_private_data);
typedef void (*fusb_interface_class_free_context_f)(void* private_data);
typedef void (*fusb_interface_class_setup_interface_f)(fusb_interface_t* interface);

FERRO_STRUCT(fusb_interface_class_methods) {
	fusb_interface_class_process_descriptor_f process_descriptor;
	fusb_interface_class_free_context_f free_context;
	fusb_interface_class_setup_interface_f setup_interface;
};

FERRO_WUR ferr_t fusb_controller_new(const fusb_controller_methods_t* methods, void* private_data, fusb_controller_t** out_controller);

FERRO_WUR ferr_t fusb_device_new(fusb_controller_t* controller, const fusb_device_methods_t* methods, void* private_data, fusb_device_t** out_device);

FERRO_WUR ferr_t fusb_device_publish(fusb_device_t* device);
FERRO_WUR ferr_t fusb_device_unpublish(fusb_device_t* device);

void fusb_device_setup(fusb_device_t* device);

FERRO_WUR ferr_t fusb_register_interface_class(uint8_t class_code, const fusb_interface_class_methods_t* methods);

typedef void (*fusb_endpoint_perform_transfer_callback_f)(void* context, ferr_t status, uint16_t transferred);

FERRO_WUR ferr_t fusb_endpoint_perform_transfer(fusb_endpoint_t* endpoint, void* physical_data, uint16_t length, fusb_endpoint_perform_transfer_callback_f callback, void* context);
FERRO_WUR ferr_t fusb_endpoint_perform_transfer_blocking(fusb_endpoint_t* endpoint, void* physical_data, uint16_t length, uint16_t* out_transferred);

FERRO_WUR ferr_t fusb_device_make_request_blocking(fusb_device_t* device, fusb_request_direction_t direction, fusb_request_type_t type, fusb_request_recipient_t recipient, fusb_request_code_t code, uint16_t value, uint16_t index, void* physical_data, uint16_t data_length);

FERRO_DECLARATIONS_END;

#endif // _FERRO_DRIVERS_USB_PRIVATE_H_
