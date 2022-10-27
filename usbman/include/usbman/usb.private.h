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

#ifndef _USBMAN_USB_PRIVATE_H_
#define _USBMAN_USB_PRIVATE_H_

#include <usbman/usb.h>
#include <usbman/objects.private.h>

USBMAN_DECLARATIONS_BEGIN;

USBMAN_STRUCT_FWD(usbman_controller);
USBMAN_STRUCT_FWD(usbman_interface);
USBMAN_STRUCT_FWD(usbman_device_object);

USBMAN_ENUM(uint8_t, usbman_request_direction) {
	usbman_request_direction_host_to_device = 0,
	usbman_request_direction_device_to_host,
};

USBMAN_ENUM(uint8_t, usbman_request_type) {
	usbman_request_type_standard = 0,
	usbman_request_type_class,
	usbman_request_type_vendor,
};

USBMAN_ENUM(uint8_t, usbman_request_recipient) {
	usbman_request_recipient_device,
	usbman_request_recipient_interface,
	usbman_request_recipient_endpoint,
	usbman_request_recipient_other,

	usbman_request_recipient_vendor_specific = 31,
};

USBMAN_ENUM(uint8_t, usbman_request_code) {
	//
	// USB2 and USB3
	//

	usbman_request_code_get_status = 0,
	usbman_request_code_clear_feature,

	usbman_request_code_set_feature = 3,

	usbman_request_code_set_address = 5,
	usbman_request_code_get_descriptor,
	usbman_request_code_set_descriptor,
	usbman_request_code_get_configuration,
	usbman_request_code_set_configuration,
	usbman_request_code_get_interface,
	usbman_request_code_set_interface,
	usbman_request_code_synch_frame,

	//
	// USB3
	//

	usbman_request_code_set_encryption,
	usbman_request_code_get_encryption,
	usbman_request_code_set_handshake,
	usbman_request_code_get_handshake,
	usbman_request_code_set_connection,
	usbman_request_code_set_security_data,
	usbman_request_code_get_security_data,
	usbman_request_code_set_wusb_data,
	usbman_request_code_loopback_data_write,
	usbman_request_code_loopback_data_read,
	usbman_request_code_set_interface_ds,

	usbman_request_code_set_sel = 48,
	usbman_request_code_set_isoch_delay,
};

USBMAN_ENUM(uint8_t, usbman_descriptor_type) {
	//
	// USB2 and USB3
	//

	usbman_descriptor_type_device = 1,
	usbman_descriptor_type_configuration,
	usbman_descriptor_type_string,
	usbman_descriptor_type_interface,
	usbman_descriptor_type_endpoint,

	//
	// USB2 only, reserved in USB3
	//

	usbman_descriptor_type_device_qualifier,
	usbman_descriptor_type_other_speed_configuration,

	//
	// USB2 and USB3
	//

	usbman_descriptor_type_interface_power,

	//
	// USB3 only
	//

	usbman_descriptor_type_otg,
	usbman_descriptor_type_debug,
	usbman_descriptor_type_interface_association,

	usbman_descriptor_type_bos = 15,
	usbman_descriptor_type_device_capability,

	usbman_descriptor_type_superspeed_usb_endpoint_companion = 48,
	usbman_descriptor_type_superspeedplus_isochronous_endpoint_companion,
};

USBMAN_ENUM(int, usbman_request_status) {
	usbman_request_status_ok = 0,
	usbman_request_status_unknown = -1,
};

USBMAN_ENUM(uint8_t, usbman_endpoint_direction) {
	usbman_endpoint_direction_out = 0,
	usbman_endpoint_direction_in,
};

USBMAN_ENUM(uint8_t, usbman_speed_id) {
	usbman_speed_id_invalid = 0,
	usbman_speed_id_full_speed,
	usbman_speed_id_low_speed,
	usbman_speed_id_high_speed,
	usbman_speed_id_super_speed_gen_1_x1,
	usbman_speed_id_super_speed_plus_gen_2_x1,
	usbman_speed_id_super_speed_plus_gen_1_x2,
	usbman_speed_id_super_speed_plus_gen_2_x2,
};

static const uint64_t usbman_maximum_bitrates[] = {
	0,
	12000000,
	1500000,
	480000000,
	5000000000,
	10000000000,
	5000000000,
	10000000000,
};

USBMAN_ENUM(uint8_t, usbman_endpoint_type) {
	usbman_endpoint_type_control = 0,
	usbman_endpoint_type_isochronous,
	usbman_endpoint_type_bulk,
	usbman_endpoint_type_interrupt,
};

USBMAN_STRUCT(usbman_device_configure_endpoint_entry) {
	uint8_t endpoint_number;
	usbman_endpoint_direction_t direction;
	uint8_t interval_power;
	uint16_t max_packet_size;
	usbman_endpoint_type_t type;
};

typedef void (*usbman_device_request_callback_f)(void* context, usbman_request_status_t status);
typedef void (*usbman_device_configure_endpoint_callback_f)(void* context, ferr_t status);
typedef void (*usbman_device_perform_transfer_callback_f)(void* context, ferr_t status, uint16_t transferred);

typedef ferr_t (*usbman_device_make_request_f)(usbman_device_object_t* device, usbman_request_direction_t direction, usbman_request_type_t type, usbman_request_recipient_t recipient, usbman_request_code_t code, uint16_t value, uint16_t index, void* physical_data, uint16_t data_length, usbman_device_request_callback_f callback, void* context);
typedef ferr_t (*usbman_device_configure_endpoints_f)(usbman_device_object_t* device, const usbman_device_configure_endpoint_entry_t* entries, size_t entry_count, usbman_device_configure_endpoint_callback_f callback, void* context);
typedef usbman_speed_id_t (*usbman_device_get_standard_speed_f)(usbman_device_object_t* device);
typedef ferr_t (*usbman_device_perform_transfer_f)(usbman_device_object_t* device, uint8_t endpoint_number, usbman_endpoint_direction_t direction, void* physical_data, uint16_t data_length, usbman_device_perform_transfer_callback_f callback, void* context);

USBMAN_STRUCT(usbman_controller_methods) {};
USBMAN_STRUCT(usbman_device_methods) {
	usbman_device_make_request_f make_request;
	usbman_device_configure_endpoints_f configure_endpoints;
	usbman_device_get_standard_speed_f get_standard_speed;
	usbman_device_perform_transfer_f perform_transfer;
};

USBMAN_STRUCT(usbman_controller) {
	void* private_data;
	const usbman_controller_methods_t* methods;
};

USBMAN_STRUCT_FWD(usbman_configuration);

USBMAN_STRUCT(usbman_device_object) {
	sys_object_t object;
	void* private_data;
	usbman_controller_t* controller;
	const usbman_device_methods_t* methods;
	uint16_t vendor_id;
	uint16_t product_id;

	usbman_device_object_t* next;
	usbman_device_object_t** prev;

	usbman_configuration_t** configurations;
	size_t configuration_count;

	usbman_configuration_t* active_configuration;
};

USBMAN_STRUCT(usbman_configuration) {
	usbman_device_object_t* device;

	usbman_interface_t** interfaces;
	size_t interface_count;

	uint8_t id;
};

USBMAN_ENUM(uint8_t, usbman_endpoint_interrupt_usage_type) {
	usbman_endpoint_interrupt_usage_type_periodic,
	usbman_endpoint_interrupt_usage_type_notification,
};

USBMAN_ENUM(uint8_t, usbman_endpoint_isochronous_usage_type) {
	usbman_endpoint_isochronous_usage_type_data,
	usbman_endpoint_isochronous_usage_type_feedback,
	usbman_endpoint_isochronous_usage_type_implicit_feedback_data,
};

USBMAN_ENUM(uint8_t, usbman_endpoint_isochronous_syncronization_type) {
	usbman_endpoint_isochronous_syncronization_type_no_synchronization,
	usbman_endpoint_isochronous_syncronization_type_asynchronous,
	usbman_endpoint_isochronous_syncronization_type_adaptive,
	usbman_endpoint_isochronous_syncronization_type_synchronous,
};

USBMAN_STRUCT_FWD(usbman_interface_setting);

USBMAN_STRUCT(usbman_endpoint) {
	usbman_interface_setting_t* setting;

	uint8_t number;
	usbman_endpoint_direction_t direction;
	usbman_endpoint_type_t type;
	uint8_t usage_type;
	uint8_t synchronization_type;
	uint8_t interval_power;
	uint16_t max_packet_size;
};

USBMAN_STRUCT_FWD(usbman_interface_class_methods);

USBMAN_STRUCT(usbman_interface_setting) {
	usbman_interface_t* interface;

	usbman_endpoint_t** endpoints;
	size_t endpoint_count;

	uint8_t id;
	uint8_t interface_class;
	uint8_t interface_subclass;
	uint8_t interface_protocol;

	void* interface_class_private_data;

	const usbman_interface_class_methods_t* interface_class_methods;
};

USBMAN_STRUCT(usbman_interface) {
	usbman_configuration_t* configuration;

	usbman_interface_setting_t** settings;
	size_t setting_count;

	uint8_t id;

	usbman_interface_setting_t* active_setting;
};

USBMAN_PACKED_STRUCT(usbman_descriptor_header) {
	uint8_t length;
	uint8_t descriptor_type;
};

USBMAN_PACKED_STRUCT(usbman_device_descriptor) {
	usbman_descriptor_header_t header;
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

USBMAN_PACKED_STRUCT(usbman_configuration_descriptor) {
	usbman_descriptor_header_t header;
	uint16_t total_length;
	uint8_t interface_count;
	uint8_t configuration_value;
	uint8_t configuration_string_index;
	uint8_t attributes;
	uint8_t max_power;
};

USBMAN_PACKED_STRUCT(usbman_interface_descriptor) {
	usbman_descriptor_header_t header;
	uint8_t interface_number;
	uint8_t alternate_setting;
	uint8_t endpoint_count;
	uint8_t interface_class;
	uint8_t interface_subclass;
	uint8_t interface_protocol;
	uint8_t interface_string_index;
};

USBMAN_PACKED_STRUCT(usbman_endpoint_descriptor) {
	usbman_descriptor_header_t header;
	uint8_t endpoint_address;
	uint8_t attributes;
	uint16_t max_packet_size;
	uint8_t interval;
};

USBMAN_PACKED_STRUCT(usbman_string_descriptor) {
	usbman_descriptor_header_t header;
	uint16_t content[];
};

typedef ferr_t (*usbman_interface_class_process_descriptor_f)(usbman_interface_setting_t* interface_setting, const usbman_descriptor_header_t* descriptor, void** in_out_private_data);
typedef void (*usbman_interface_class_free_context_f)(void* private_data);
typedef void (*usbman_interface_class_setup_interface_f)(usbman_interface_t* interface);

USBMAN_STRUCT(usbman_interface_class_methods) {
	usbman_interface_class_process_descriptor_f process_descriptor;
	usbman_interface_class_free_context_f free_context;
	usbman_interface_class_setup_interface_f setup_interface;
};

USBMAN_WUR ferr_t usbman_controller_new(const usbman_controller_methods_t* methods, void* private_data, usbman_controller_t** out_controller);

USBMAN_WUR ferr_t usbman_device_new(usbman_controller_t* controller, const usbman_device_methods_t* methods, void* private_data, usbman_device_object_t** out_device);

USBMAN_WUR ferr_t usbman_device_publish(usbman_device_object_t* device);
USBMAN_WUR ferr_t usbman_device_unpublish(usbman_device_object_t* device);

void usbman_device_setup(usbman_device_object_t* device);

USBMAN_WUR ferr_t usbman_register_interface_class(uint8_t class_code, const usbman_interface_class_methods_t* methods);

typedef void (*usbman_endpoint_perform_transfer_callback_f)(void* context, ferr_t status, uint16_t transferred);

USBMAN_WUR ferr_t usbman_endpoint_perform_transfer(usbman_endpoint_t* endpoint, void* physical_data, uint16_t length, usbman_endpoint_perform_transfer_callback_f callback, void* context);
USBMAN_WUR ferr_t usbman_endpoint_perform_transfer_blocking(usbman_endpoint_t* endpoint, void* physical_data, uint16_t length, uint16_t* out_transferred);

USBMAN_WUR ferr_t usbman_device_make_request_blocking(usbman_device_object_t* device, usbman_request_direction_t direction, usbman_request_type_t type, usbman_request_recipient_t recipient, usbman_request_code_t code, uint16_t value, uint16_t index, void* physical_data, uint16_t data_length);

USBMAN_DECLARATIONS_END;

#endif // _USBMAN_DRIVERS_USB_PRIVATE_H_
