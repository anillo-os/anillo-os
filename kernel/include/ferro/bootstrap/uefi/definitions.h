/**
 * This file is part of Anillo OS
 * Copyright (C) 2020 Anillo OS Developers
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

#ifndef _FERRO_BOOTSTRAP_UEFI_DEFINITIONS_H_
#define _FERRO_BOOTSTRAP_UEFI_DEFINITIONS_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <limits.h>

#include <ferro/base.h>
#include <ferro/platform.h>

FERRO_DECLARATIONS_BEGIN;

//
// basic types
//

/**
 * 128-bit immutable GUID string
 */
typedef const uint8_t* fuefi_guid_c;

/**
 * UEFI status type
 */
typedef size_t fuefi_status_t;

/**
 * Mutable generic UEFI data pointer
 */
typedef void* fuefi_handle_t;

/**
 * Immutable generic UEFI data pointer
 */
typedef const void* fuefi_handle_c;

/**
 * Mutable UEFI event descriptor pointer
 */
typedef fuefi_handle_t fuefi_event_t;

/**
 * Immutable UEFI event descriptor pointer
 */
typedef fuefi_handle_c fuefi_event_c;

/**
 * Mutable UEFI image descriptor pointer
 */
typedef fuefi_handle_t fuefi_image_handle_t;

/**
 * Immutable UEFI image descriptor pointer
 */
typedef fuefi_handle_c fuefi_image_handle_c;

/**
 * Memory map key
 */
typedef size_t fuefi_memory_map_key_t;

//
// useful macros
//

#define FUEFI_WUR __attribute__((warn_unused_result))

#define FUEFI_API __attribute__((ms_abi))
#define FUEFI_FUNCPTR_ATTRS(_attributes, _return_type, _name, ...) _return_type (_attributes *_name)(__VA_ARGS__)
#define FUEFI_FUNCPTR(_return_type, _name, ...) FUEFI_FUNCPTR_ATTRS(FUEFI_API, _return_type, _name, __VA_ARGS__)
#define FUEFI_METHOD_ATTRS(_attributes, _this_type, _return_type, _name, ...) FUEFI_FUNCPTR_ATTRS(_attributes, _return_type, _name, _this_type* self, ## __VA_ARGS__)
#define FUEFI_METHOD(_this_type, _return_type, _name, ...) FUEFI_METHOD_ATTRS(FUEFI_API, _this_type, _return_type, _name, ## __VA_ARGS__)
#define FUEFI_STATUS_METHOD(_this_type, _name, ...) FUEFI_METHOD_ATTRS(FUEFI_API FUEFI_WUR, _this_type, fuefi_status_t, _name, ## __VA_ARGS__)
#define FUEFI_STATUS_FUNCPTR(_name, ...) FUEFI_FUNCPTR_ATTRS(FUEFI_API FUEFI_WUR, fuefi_status_t, _name, ## __VA_ARGS__)

#define FUEFI_GUID(_name, ...) static uint8_t _name[] = { __VA_ARGS__ }

//
// memory types
//

FERRO_ENUM(uint32_t, fuefi_memory_type) {
	fuefi_memory_type_reserved,
	fuefi_memory_type_loader_code,
	fuefi_memory_type_loader_data,
	fuefi_memory_type_bs_code,
	fuefi_memory_type_bs_data,
	fuefi_memory_type_rs_code,
	fuefi_memory_type_rs_data,
	fuefi_memory_type_generic,
	fuefi_memory_type_unusable,
	fuefi_memory_type_acpi_reclaimable,
	fuefi_memory_type_acpi,
	fuefi_memory_type_mmio,
	fuefi_memory_type_mmio_port_space,
	fuefi_memory_type_processor_reserved,
	fuefi_memory_type_nvram,
};

//
// simple text input protocol
//

FERRO_STRUCT(fuefi_simple_text_input_keystroke) {
	uint16_t scancode;
	wchar_t unichar;
};

FERRO_STRUCT(fuefi_simple_text_input_protocol) {
	FUEFI_STATUS_METHOD(fuefi_simple_text_input_protocol_t, reset, bool use_extended_verification);
	FUEFI_STATUS_METHOD(fuefi_simple_text_input_protocol_t, read_keystroke, fuefi_simple_text_input_keystroke_t* out_keystroke);
	fuefi_event_t wait_for_key_event;
};

//
// simple text output protocol
//

FERRO_STRUCT(fuefi_simple_text_output_mode) {
	uint32_t max_mode;

	uint32_t mode;
	uint32_t attribute;
	uint32_t column;
	uint32_t row;
	bool is_cursor_visible;
};

FERRO_STRUCT(fuefi_simple_text_output_protocol) {
	FUEFI_STATUS_METHOD(fuefi_simple_text_output_protocol_t, reset, bool use_extended_verification);
	FUEFI_STATUS_METHOD(fuefi_simple_text_output_protocol_t, output_string, const wchar_t* string);
	FUEFI_STATUS_METHOD(fuefi_simple_text_output_protocol_t, test_string, const wchar_t* string);
	FUEFI_STATUS_METHOD(fuefi_simple_text_output_protocol_t, query_mode, size_t mode, size_t* out_columns, size_t* out_rows);
	FUEFI_STATUS_METHOD(fuefi_simple_text_output_protocol_t, set_mode, size_t mode);
	FUEFI_STATUS_METHOD(fuefi_simple_text_output_protocol_t, set_attribute, size_t attribute);
	FUEFI_STATUS_METHOD(fuefi_simple_text_output_protocol_t, clear_screen);
	FUEFI_STATUS_METHOD(fuefi_simple_text_output_protocol_t, set_cursor_position, size_t column, size_t row);
	FUEFI_STATUS_METHOD(fuefi_simple_text_output_protocol_t, enable_cursor, bool is_enabled);
};

//
// generic shared table header
//

FERRO_STRUCT(fuefi_table_header) {
	uint64_t signature;
	uint32_t revision;
	uint32_t table_size; // includes the size of this header
	uint32_t crc32;
	uint32_t reserved;
};

//
// boot services
//

FERRO_OPTIONS(uint32_t, fuefi_event_type) {
	fuefi_event_type_timer                            = 0x80000000U,
	fuefi_event_type_runtime                          = 0x40000000U,
	fuefi_event_type_runtime_context                  = 0x20000000U,

	fuefi_event_type_notify_wait                      = 0x00000100U,
	fuefi_event_type_notify_signal                    = 0x00000200U,

	// these are only here to get clang to shut up
	fuefi_event_type_notify_on_ebs_bit                = 0x00000001U,
	fuefi_event_type_notify_on_virtual_map_change_bit = 0x00000002U,

	fuefi_event_type_notify_on_ebs                    = 0x00000201U,
	fuefi_event_type_notify_on_virtual_map_change     = 0x60000202U,
};

FERRO_ENUM(size_t, fuefi_tpl) {
	fuefi_tpl_application = 4,
	fuefi_tpl_callback = 8,
	fuefi_tpl_notify = 16,
	fuefi_tpl_high_level = 31,
};

FERRO_ENUM(uint32_t, fuefi_timer_delay) {
	fuefi_timer_delay_cancel,
	fuefi_timer_delay_periodic,
	fuefi_timer_delay_relative,
};

typedef FUEFI_FUNCPTR(void, fuefi_event_notification_handler_f, fuefi_event_t event, void* context);

FERRO_ENUM(uint32_t, fuefi_memory_allocation_type) {
	fuefi_memory_allocation_type_any_pages,
	fuefi_memory_allocation_type_max_address,
	fuefi_memory_allocation_type_fixed_address,
};

FERRO_STRUCT(fuefi_memory_descriptor) {
	fuefi_memory_type_t type;
	void* physical_start;
	void* virtual_start;
	uint64_t page_count;
	uint64_t attribute;
};

FERRO_ENUM(uint32_t, fuefi_interface_type) {
	fuefi_interface_type_native,
};

FERRO_ENUM(uint32_t, fuefi_locate_search_type) {
	fuefi_locate_search_type_all_handles,
	fuefi_locate_search_type_registration,
	fuefi_locate_search_type_protocol,
};

FERRO_STRUCT(fuefi_protocol_information_entry) {
	fuefi_handle_t agent_handle;
	fuefi_handle_t controller_handle;
	uint32_t attributes;
	uint32_t open_count;
};

FERRO_STRUCT(fuefi_device_path_protocol) {
	uint8_t type;
	uint8_t subtype;
	uint16_t length;
};

FERRO_STRUCT(fuefi_boot_services) {
	fuefi_table_header_t header;

	FUEFI_FUNCPTR(fuefi_tpl_t, raise_tpl, fuefi_tpl_t tpl);
	FUEFI_FUNCPTR(void, restore_tpl, fuefi_tpl_t tpl);

	FUEFI_STATUS_FUNCPTR(allocate_pages, fuefi_memory_allocation_type_t allocation_type, fuefi_memory_type_t memory_type, size_t page_count, void** in_out_address);
	FUEFI_STATUS_FUNCPTR(free_pages, void* address, size_t page_count);
	FUEFI_STATUS_FUNCPTR(get_memory_map, size_t* in_out_map_size, fuefi_memory_descriptor_t* in_out_descriptors, fuefi_memory_map_key_t* out_map_key, size_t* out_descriptor_size, uint32_t* out_version);
	FUEFI_STATUS_FUNCPTR(allocate_pool, fuefi_memory_type_t type, size_t size, void** out_buffer);
	FUEFI_STATUS_FUNCPTR(free_pool, void** buffer);

	FUEFI_STATUS_FUNCPTR(create_event, fuefi_event_type_t type, fuefi_tpl_t notify_tpl, fuefi_event_notification_handler_f notification_handler, void* notification_context, fuefi_event_t* out_event);
	FUEFI_STATUS_FUNCPTR(set_timer, fuefi_event_t event, fuefi_timer_delay_t delay, uint64_t trigger_time);
	FUEFI_STATUS_FUNCPTR(wait_for_event, size_t event_count, fuefi_event_t* events, size_t* out_index);
	FUEFI_STATUS_FUNCPTR(signal_event, fuefi_event_t event);
	FUEFI_STATUS_FUNCPTR(close_event, fuefi_event_t event);
	FUEFI_STATUS_FUNCPTR(check_event, fuefi_event_t event);

	FUEFI_STATUS_FUNCPTR(install_protocol, fuefi_handle_t* in_out_handle, fuefi_guid_c protocol, fuefi_interface_type_t type, void* interface);
	FUEFI_STATUS_FUNCPTR(reinstall_protocol, fuefi_handle_t handle, fuefi_guid_c protocol, void* old_interface, void* new_interface);
	FUEFI_STATUS_FUNCPTR(uninstall_protocol, fuefi_handle_t handle, fuefi_guid_c protocol, void* interface);
	FUEFI_STATUS_FUNCPTR(handle_protocol, fuefi_handle_t handle, fuefi_guid_c protocol, void** out_interface);
	const void* reserved;
	FUEFI_STATUS_FUNCPTR(register_protocol_notification, fuefi_guid_c protocol, fuefi_event_t event, void** out_registration);
	FUEFI_STATUS_FUNCPTR(locate_handle, fuefi_locate_search_type_t search_type, fuefi_guid_c protocol, void* registration, size_t* in_out_array_size, fuefi_handle_t* out_array);
	FUEFI_STATUS_FUNCPTR(locate_device_path, fuefi_guid_c protocol, fuefi_device_path_protocol_t* in_out_path, fuefi_handle_t out_handle);
	FUEFI_STATUS_FUNCPTR(install_configuration_table, fuefi_guid_c guid, void* table);

	FUEFI_STATUS_FUNCPTR(load_image, bool is_boot_policy, fuefi_image_handle_t parent_image, fuefi_device_path_protocol_t* device_path, void* image_buffer, size_t image_size, fuefi_image_handle_t* out_image_handle);
	FUEFI_STATUS_FUNCPTR(start_image, fuefi_image_handle_t image_handle, size_t* out_exit_data_size, wchar_t** out_exit_data);
	FUEFI_STATUS_FUNCPTR(exit, fuefi_image_handle_t image_handle, fuefi_status_t exit_status, size_t exit_data_size, wchar_t* exit_data);
	FUEFI_STATUS_FUNCPTR(unload_image, fuefi_image_handle_t image_handle);
	FUEFI_STATUS_FUNCPTR(exit_boot_services, fuefi_image_handle_t image_handle, fuefi_memory_map_key_t map_key);

	FUEFI_STATUS_FUNCPTR(get_next_monotonic_count, uint64_t* out_count);
	FUEFI_STATUS_FUNCPTR(stall, size_t microseconds);
	FUEFI_STATUS_FUNCPTR(set_watchdog_timer, size_t timeout, uint64_t code, size_t data_size, wchar_t* data);

	FUEFI_STATUS_FUNCPTR(connect_controller, fuefi_handle_t handle, fuefi_handle_t* driver_image_handles, fuefi_device_path_protocol_t* remaining_path, bool do_recursively);
	FUEFI_STATUS_FUNCPTR(disconnect_controller, fuefi_handle_t handle, fuefi_handle_t driver_image_handle, fuefi_handle_t child_handle);

	FUEFI_STATUS_FUNCPTR(open_protocol, fuefi_handle_t handle, fuefi_guid_c protocol, void** out_interface, fuefi_handle_t agent_handle, fuefi_handle_t controller_handle, uint32_t attributes);
	FUEFI_STATUS_FUNCPTR(close_protocol, fuefi_handle_t handle, fuefi_guid_c protocol, fuefi_handle_t agent_handle, fuefi_handle_t controller_handle);
	FUEFI_STATUS_FUNCPTR(open_protocol_information, fuefi_handle_t handle, fuefi_guid_c protocol, fuefi_protocol_information_entry_t** out_array, size_t* out_entry_count);

	FUEFI_STATUS_FUNCPTR(protocols_per_handle, fuefi_handle_t handle, fuefi_guid_c** out_protocol_array, size_t* out_protocol_count);
	FUEFI_STATUS_FUNCPTR(locate_handle_buffer, fuefi_locate_search_type_t type, fuefi_guid_c protocol, void* registration, size_t* in_out_handle_count, fuefi_handle_t** out_handle_array);
	FUEFI_STATUS_FUNCPTR(locate_protocol, fuefi_guid_c protocol, void* registration, void** out_interface);
	FUEFI_STATUS_FUNCPTR(install_multiple_protocols, fuefi_handle_t* in_out_handle, ...);
	FUEFI_STATUS_FUNCPTR(uninstall_multiple_protocols, fuefi_handle_t handle, ...);

	FUEFI_STATUS_FUNCPTR(calculate_crc32, const void* data, size_t data_size, uint32_t* out_crc32);

	FUEFI_FUNCPTR(void, copy_memory, void* destination, const void* source, size_t count);
	FUEFI_FUNCPTR(void, set_memory, void* destination, size_t count, uint8_t value);
	FUEFI_STATUS_FUNCPTR(create_event_extended, fuefi_event_type_t type, fuefi_tpl_t notification_tpl, fuefi_event_notification_handler_f notification_handler, void* notification_context, fuefi_guid_c event_group, fuefi_event_t* out_event);
};

//
// runtime services
//

FERRO_STRUCT(fuefi_time) {
	uint16_t year;
	uint8_t month;
	uint8_t day;
	uint8_t hour;
	uint8_t minute;
	uint8_t second;
	uint8_t padding_1;
	uint32_t nanosecond;
	int16_t timezone;
	uint8_t dst;
	uint8_t padding_2;
};

FERRO_STRUCT(fuefi_time_capabilities) {
	uint32_t resolution;
	uint32_t accuracy;
	bool resets_on_low_resolution;
};

FERRO_ENUM(uint32_t, fuefi_reset_type) {
	fuefi_reset_type_cold,
	fuefi_reset_type_warm,
	fuefi_reset_type_shutdown,
	fuefi_reset_type_platform_specific,
};

FERRO_STRUCT(fuefi_capsule_header) {
	uint8_t guid[16];
	uint32_t header_size;
	uint32_t flags;
	uint32_t image_size;
};

FERRO_STRUCT(fuefi_runtime_services) {
	fuefi_table_header_t header;

	FUEFI_STATUS_FUNCPTR(get_time, fuefi_time_t* out_time, fuefi_time_capabilities_t* out_time_capabilities);
	FUEFI_STATUS_FUNCPTR(set_time, fuefi_time_t* time);
	FUEFI_STATUS_FUNCPTR(get_wakeup_time, bool* out_is_enabled, bool* out_is_pending, fuefi_time_t* out_time);
	FUEFI_STATUS_FUNCPTR(set_wakeup_time, bool* is_enabled, fuefi_time_t* time);

	FUEFI_STATUS_FUNCPTR(set_virtual_address_map, size_t memory_map_size, size_t descriptor_size, uint32_t descriptor_version, fuefi_memory_descriptor_t* descriptors);
	FUEFI_STATUS_FUNCPTR(convert_pointer, size_t debug_disposition, void** in_out_address);

	FUEFI_STATUS_FUNCPTR(get_variable, const wchar_t* name, fuefi_guid_c vendor, uint32_t* out_attributes, size_t* in_out_data_size, void* out_data);
	FUEFI_STATUS_FUNCPTR(get_next_variable_name, size_t* in_out_name_size, wchar_t* in_out_name, fuefi_guid_c* in_out_vendor);
	FUEFI_STATUS_FUNCPTR(set_variable, const wchar_t* name, fuefi_guid_c vendor, uint32_t attributes, size_t data_size, void* data);

	FUEFI_STATUS_FUNCPTR(get_next_high_monotonic_count, uint32_t* out_count);
	FUEFI_FUNCPTR(void, reset_system, fuefi_reset_type_t type, fuefi_status_t status, size_t data_size, void* data);

	FUEFI_STATUS_FUNCPTR(update_capsule, fuefi_capsule_header_t* header_array, size_t count, void* scatter_gather_list);
	FUEFI_STATUS_FUNCPTR(query_capsule_capabilities, fuefi_capsule_header_t* header_array, size_t count, uint64_t* out_max_size, fuefi_reset_type_t* out_type);

	FUEFI_STATUS_FUNCPTR(query_variable_info, uint32_t attributes, uint64_t* out_max_size, uint64_t* out_free_size, uint64_t* out_max_individual_size);
};

//
// configuration table
//

FERRO_STRUCT(fuefi_configuration_table_entry) {
	uint8_t guid[16];
	void* table;
};

//
// system table
//

FERRO_STRUCT(fuefi_system_table) {
	fuefi_table_header_t header;

	const wchar_t* fw_vendor;
	uint32_t fw_revision;

	fuefi_handle_t console_input_handle;
	fuefi_simple_text_input_protocol_t* console_input;

	fuefi_handle_t console_output_handle;
	fuefi_simple_text_output_protocol_t* console_output;

	fuefi_handle_t console_error_handle;
	fuefi_simple_text_output_protocol_t* console_error;

	fuefi_runtime_services_t* runtime_services;
	fuefi_boot_services_t* boot_services;

	size_t configuration_table_entry_count;
	fuefi_configuration_table_entry_t* configuration_table;
};

//
// status codes
//

#if FERRO_BITNESS == FERRO_BITNESS_64
	#define FUEFI_ERROR(value) ((size_t)0x8000000000000000UL | (size_t)value)
#else
	#define FUEFI_ERROR(value) ((size_t)0x80000000U | (size_t)value)
#endif

typedef size_t fuefi_status_t;

// these error codes must be defined instead of enumerated because of size issues

#define fuefi_status_ok 0

#define fuefi_status_load_error           FUEFI_ERROR( 1)
#define fuefi_status_invalid_parameter    FUEFI_ERROR( 2)
#define fuefi_status_unsupported          FUEFI_ERROR( 3)
#define fuefi_status_bad_buffer_size      FUEFI_ERROR( 4)
#define fuefi_status_buffer_too_small     FUEFI_ERROR( 5)
#define fuefi_status_not_ready            FUEFI_ERROR( 6)
#define fuefi_status_device_error         FUEFI_ERROR( 7)
#define fuefi_status_write_protected      FUEFI_ERROR( 8)
#define fuefi_status_out_of_resources     FUEFI_ERROR( 9)
#define fuefi_status_volume_corrupted     FUEFI_ERROR(10)
#define fuefi_status_volume_full          FUEFI_ERROR(11)
#define fuefi_status_no_media             FUEFI_ERROR(12)
#define fuefi_status_media_changed        FUEFI_ERROR(13)
#define fuefi_status_not_found            FUEFI_ERROR(14)
#define fuefi_status_access_denied        FUEFI_ERROR(15)
#define fuefi_status_no_response          FUEFI_ERROR(16)
#define fuefi_status_no_mapping           FUEFI_ERROR(17)
#define fuefi_status_timeout              FUEFI_ERROR(18)
#define fuefi_status_not_started          FUEFI_ERROR(19)
#define fuefi_status_already_started      FUEFI_ERROR(20)
#define fuefi_status_aborted              FUEFI_ERROR(21)
#define fuefi_status_icmp_error           FUEFI_ERROR(22)
#define fuefi_status_tftp                 FUEFI_ERROR(23)
#define fuefi_status_protocol_error       FUEFI_ERROR(24)
#define fuefi_status_incompatible_version FUEFI_ERROR(25)
#define fuefi_status_security_violation   FUEFI_ERROR(26)
#define fuefi_status_crc_error            FUEFI_ERROR(27)
#define fuefi_status_end_of_media         FUEFI_ERROR(28)
#define fuefi_status_end_of_file          FUEFI_ERROR(31)
#define fuefi_status_invalid_language     FUEFI_ERROR(32)
#define fuefi_status_compromised_data     FUEFI_ERROR(33)
#define fuefi_status_ip_address_conflict  FUEFI_ERROR(34)
#define fuefi_status_http_error           FUEFI_ERROR(35)

#define fuefi_status_unknown_glyph            1
#define fuefi_status_delete_failure           2
#define fuefi_status_write_failure            3
#define fuefi_status_buffer_too_small_warning 4
#define fuefi_status_stale_data               5
#define fuefi_status_file_system              6

//
// loaded image protocol
//

FERRO_STRUCT(fuefi_loaded_image_protocol) {
	uint32_t revision;
	fuefi_image_handle_t parent;
	fuefi_system_table_t* system_table;

	fuefi_handle_t source_device;
	fuefi_device_path_protocol_t* source_path;
	void* reserved;

	uint32_t load_options_size;
	void* load_options;

	void* image_base;
	uint64_t image_size;
	fuefi_memory_type_t code_type;
	fuefi_memory_type_t data_type;

	FUEFI_STATUS_FUNCPTR(unload, fuefi_image_handle_t image_handle);
};

//
// simple filesystem protocol
//

FERRO_OPTIONS(uint64_t, fuefi_file_mode) {
	fuefi_file_mode_read   = 1,
	fuefi_file_mode_write  = 2,
	fuefi_file_mode_create = 0x8000000000000000UL,
};

FERRO_STRUCT(fuefi_file_io_token) {
	fuefi_event_t event;
	fuefi_status_t status;
	size_t buffer_size;
	void* buffer;
};

FERRO_STRUCT(fuefi_file_protocol) {
	uint64_t revision;
	FUEFI_STATUS_METHOD(fuefi_file_protocol_t, open, fuefi_file_protocol_t** out_result_handle, const wchar_t* filename, fuefi_file_mode_t mode, uint64_t attributes);
	FUEFI_STATUS_METHOD(fuefi_file_protocol_t, close);
	FUEFI_STATUS_METHOD(fuefi_file_protocol_t, delete);
	FUEFI_STATUS_METHOD(fuefi_file_protocol_t, read, size_t* in_out_size, void* buffer);
	FUEFI_STATUS_METHOD(fuefi_file_protocol_t, write, size_t* in_out_size, const void* buffer);
	FUEFI_STATUS_METHOD(fuefi_file_protocol_t, get_position, uint64_t* out_position);
	FUEFI_STATUS_METHOD(fuefi_file_protocol_t, set_position, uint64_t position);
	FUEFI_STATUS_METHOD(fuefi_file_protocol_t, get_info, fuefi_guid_c info_type, size_t* in_out_buffer_size, void* out_buffer);
	FUEFI_STATUS_METHOD(fuefi_file_protocol_t, set_info, fuefi_guid_c info_type, size_t buffer_size, const void* buffer);
	FUEFI_STATUS_METHOD(fuefi_file_protocol_t, flush);
	FUEFI_STATUS_METHOD(fuefi_file_protocol_t, open_extended, fuefi_file_protocol_t** out_result_handle, const wchar_t* filename, fuefi_file_mode_t mode, uint64_t attributes, fuefi_file_io_token_t* in_out_token);
	FUEFI_STATUS_METHOD(fuefi_file_protocol_t, read_extended, fuefi_file_io_token_t* in_out_token);
	FUEFI_STATUS_METHOD(fuefi_file_protocol_t, write_extended, fuefi_file_io_token_t* in_out_token);
	FUEFI_STATUS_METHOD(fuefi_file_protocol_t, flush_extended, fuefi_file_io_token_t* in_out_token);
};

FERRO_STRUCT(fuefi_simple_filesystem_protocol) {
	uint64_t revision;
	FUEFI_STATUS_METHOD(fuefi_simple_filesystem_protocol_t, open_volume, fuefi_file_protocol_t** out_root);
};

//
// graphics output protocol
//

FERRO_ENUM(uint32_t, fuefi_graphics_pixel_format) {
	fuefi_graphics_pixel_format_rgb,
	fuefi_graphics_pixel_format_bgr,
	fuefi_graphics_pixel_format_bitmask,
	fuefi_graphics_pixel_format_blt_only,
};

FERRO_STRUCT(fuefi_graphics_pixel_bitmask) {
	uint32_t red;
	uint32_t green;
	uint32_t blue;
	uint32_t reserved;
};

FERRO_STRUCT(fuefi_graphics_output_protocol_mode_info) {
	uint32_t version;
	uint32_t width;
	uint32_t height;
	fuefi_graphics_pixel_format_t format;
	fuefi_graphics_pixel_bitmask_t bitmask;
	uint32_t pixels_per_scanline;
};

FERRO_STRUCT(fuefi_graphics_output_protocol_mode) {
	uint32_t max_mode;
	uint32_t mode;
	fuefi_graphics_output_protocol_mode_info_t* info;
	size_t info_size; // size of this structure
	void* framebuffer_phys_addr;
	size_t framebuffer_size;
};

FERRO_STRUCT(fuefi_graphics_output_pixel) {
	uint8_t blue;
	uint8_t green;
	uint8_t red;
	uint8_t reserved;
};

FERRO_ENUM(uint32_t, fuefi_graphics_output_operation) {
	fuefi_graphics_output_operation_video_fill,
	fuefi_graphics_output_operation_video_to_buffer,
	fuefi_graphics_output_operation_buffer_to_video,
	fuefi_graphics_output_operation_video_to_video,
};

FERRO_STRUCT(fuefi_graphics_output_protocol) {
	FUEFI_STATUS_METHOD(fuefi_graphics_output_protocol_t, query_mode, uint32_t mode, size_t* out_info_size, fuefi_graphics_output_protocol_mode_info_t** out_info);
	FUEFI_STATUS_METHOD(fuefi_graphics_output_protocol_t, set_mode, uint32_t mode);
	FUEFI_STATUS_METHOD(fuefi_graphics_output_protocol_t, block_transfer, fuefi_graphics_output_pixel_t* pixel_array, fuefi_graphics_output_operation_t operation, size_t source_x, size_t source_y, size_t destination_x, size_t destination_y, size_t width, size_t height, size_t delta);
	fuefi_graphics_output_protocol_mode_t* mode;
};

//
// GUIDs
//

FUEFI_GUID(     fuefi_guid_loaded_image_protocol, 0xa1, 0x31, 0x1b, 0x5b, 0x62, 0x95, 0xd2, 0x11, 0x8e, 0x3f, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b);
FUEFI_GUID(fuefi_guid_simple_filesystem_protocol, 0x22, 0x5b, 0x4e, 0x96, 0x59, 0x64, 0xd2, 0x11, 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b);
FUEFI_GUID(  fuefi_guid_graphics_output_protocol, 0xde, 0xa9, 0x42, 0x90, 0xdc, 0x23, 0x38, 0x4a, 0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a);
FUEFI_GUID(             fuefi_guid_acpi_20_table, 0x71, 0xe8, 0x68, 0x88, 0xf1, 0xe4, 0xd3, 0x11, 0xbc, 0x22, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81);
FUEFI_GUID(             fuefi_guid_acpi_10_table, 0x30, 0x2d, 0x9d, 0xeb, 0x88, 0x2d, 0xd3, 0x11, 0x9a, 0x16, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d);

FERRO_DECLARATIONS_END;

#endif // _FERRO_BOOTSTRAP_UEFI_DEFINITIONS_H_
