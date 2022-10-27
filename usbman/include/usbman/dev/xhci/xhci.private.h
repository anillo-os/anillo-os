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

#ifndef _USBMAN_DEV_XHCI_XHCI_PRIVATE_H_
#define _USBMAN_DEV_XHCI_XHCI_PRIVATE_H_

#include <usbman/dev/xhci/xhci.h>
#include <libsimple/libsimple.h>
#include <usbman/usb.private.h>
#include <libeve/libeve.h>
#include <libpci/libpci.h>

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

USBMAN_DECLARATIONS_BEGIN;

USBMAN_PACKED_STRUCT(usbman_xhci_controller_capability_registers) {
	volatile uint32_t length_and_version;
	volatile uint32_t hcs_params[3];
	volatile uint32_t hcc_params_1;
	volatile uint32_t doorbell_offset;
	volatile uint32_t runtime_register_space_offset;
	volatile uint32_t hcc_params_2;
};

USBMAN_ALWAYS_INLINE
uint8_t usbman_xhci_controller_capability_registers_length(volatile usbman_xhci_controller_capability_registers_t* cap_regs) {
	return cap_regs->length_and_version & 0xff;
};

USBMAN_ALWAYS_INLINE
uint16_t usbman_xhci_controller_capability_registers_version(volatile usbman_xhci_controller_capability_registers_t* cap_regs) {
	return cap_regs->length_and_version >> 16;
};

USBMAN_ENUM(uint32_t, usbman_xhci_controller_hcs_parameter_1_flags) {
	usbman_xhci_controller_hcs_parameter_1_flag_scratchpad_restore = 1 << 26,
};

USBMAN_ALWAYS_INLINE
uint8_t usbman_xhci_controller_capability_registers_max_device_slots(volatile usbman_xhci_controller_capability_registers_t* cap_regs) {
	return cap_regs->hcs_params[0] & 0xff;
};

USBMAN_ALWAYS_INLINE
uint16_t usbman_xhci_controller_capability_registers_max_interrupters(volatile usbman_xhci_controller_capability_registers_t* cap_regs) {
	return (cap_regs->hcs_params[0] >> 8) & 0x7ff;
};

USBMAN_ALWAYS_INLINE
uint8_t usbman_xhci_controller_capability_registers_max_ports(volatile usbman_xhci_controller_capability_registers_t* cap_regs) {
	return (cap_regs->hcs_params[0] >> 24) & 0xff;
};

USBMAN_ALWAYS_INLINE
uint8_t usbman_xhci_controller_capability_registers_ist(volatile usbman_xhci_controller_capability_registers_t* cap_regs) {
	return cap_regs->hcs_params[1] & 0x0f;
};

USBMAN_ALWAYS_INLINE
uint8_t usbman_xhci_controller_capability_registers_erst_max(volatile usbman_xhci_controller_capability_registers_t* cap_regs) {
	return (cap_regs->hcs_params[1] >> 4) & 0x0f;
};

USBMAN_ALWAYS_INLINE
uint16_t usbman_xhci_controller_capability_registers_max_scratchpad_buffers(volatile usbman_xhci_controller_capability_registers_t* cap_regs) {
	return (((cap_regs->hcs_params[1] >> 21) & 0x1f) << 5) | ((cap_regs->hcs_params[1] >> 27) & 0x1f);
};

USBMAN_ALWAYS_INLINE
uint8_t usbman_xhci_controller_capability_registers_u1_device_exit_latency(volatile usbman_xhci_controller_capability_registers_t* cap_regs) {
	return cap_regs->hcs_params[2] & 0xff;
};

USBMAN_ALWAYS_INLINE
uint8_t usbman_xhci_controller_capability_registers_u2_device_exit_latency(volatile usbman_xhci_controller_capability_registers_t* cap_regs) {
	return (cap_regs->hcs_params[2] >> 8) & 0xff;
};

USBMAN_ENUM(uint32_t, usbman_xhci_controller_hcc_parameter_1_flags) {
	usbman_xhci_controller_hcc_parameter_1_flag_is_64bit                              = 1 <<  0,
	usbman_xhci_controller_hcc_parameter_1_flag_can_negotiate_bandwidth               = 1 <<  1,
	usbman_xhci_controller_hcc_parameter_1_flag_uses_large_context_data_structures    = 1 <<  2,
	usbman_xhci_controller_hcc_parameter_1_flag_has_port_power_control                = 1 <<  3,
	usbman_xhci_controller_hcc_parameter_1_flag_supports_port_indicator_control       = 1 <<  4,
	usbman_xhci_controller_hcc_parameter_1_flag_supports_light_reset                  = 1 <<  5,
	usbman_xhci_controller_hcc_parameter_1_flag_supports_latency_tolerance_messaging  = 1 <<  6,
	usbman_xhci_controller_hcc_parameter_1_flag_does_not_support_secondary_stream_ids = 1 <<  7,
	usbman_xhci_controller_hcc_parameter_1_flag_parses_all_event_data                 = 1 <<  8,
	usbman_xhci_controller_hcc_parameter_1_flag_can_generate_stopped_short_packet     = 1 <<  9,
	usbman_xhci_controller_hcc_parameter_1_flag_supports_stopped_edtla                = 1 << 10,
	usbman_xhci_controller_hcc_parameter_1_flag_contiguous_frame_id_capable           = 1 << 11,
};

USBMAN_ALWAYS_INLINE
uint8_t usbman_xhci_controller_capability_registers_max_primary_stream_array_size(volatile usbman_xhci_controller_capability_registers_t* cap_regs) {
	return (cap_regs->hcc_params_1 >> 12) & 0x0f;
};

USBMAN_ALWAYS_INLINE
uint16_t usbman_xhci_controller_capability_registers_extended_capabilities_pointer(volatile usbman_xhci_controller_capability_registers_t* cap_regs) {
	return cap_regs->hcc_params_1 >> 16;
};

USBMAN_ENUM(uint32_t, usbman_xhci_controller_hcc_parameter_2_flags) {
	usbman_xhci_controller_hcc_parameter_2_flag_supports_port_suspend_complete_notification = 1 << 0,
	usbman_xhci_controller_hcc_parameter_2_flag_can_generate_max_exit_latency_too_large     = 1 << 1,
	usbman_xhci_controller_hcc_parameter_2_flag_supports_force_save_context                 = 1 << 2,
	usbman_xhci_controller_hcc_parameter_2_flag_supports_compliance_transition_enabled      = 1 << 3,
	usbman_xhci_controller_hcc_parameter_2_flag_supports_large_esit_payloads                = 1 << 4,
	usbman_xhci_controller_hcc_parameter_2_flag_supports_extended_configuration_info        = 1 << 5,
	usbman_xhci_controller_hcc_parameter_2_flag_supports_extended_tbc                       = 1 << 6,
	usbman_xhci_controller_hcc_parameter_2_flag_supports_extended_tbc_trb_status            = 1 << 7,
	usbman_xhci_controller_hcc_parameter_2_flag_supports_extended_properties                = 1 << 8,
	usbman_xhci_controller_hcc_parameter_2_flag_supports_vtio                               = 1 << 9,
};

USBMAN_PACKED_STRUCT(usbman_xhci_port_register_set) {
	volatile uint32_t status_and_control;
	volatile uint32_t power_management_status_and_control;
	volatile uint32_t link_info;
	volatile uint32_t hardware_lpm_control;
};

USBMAN_ENUM(uint32_t, usbman_xhci_port_status_and_control_flags) {
	usbman_xhci_port_status_and_control_flag_current_connect_status     = 1 <<  0,
	usbman_xhci_port_status_and_control_flag_port_enabled               = 1 <<  1,
	usbman_xhci_port_status_and_control_flag_overcurrent_active         = 1 <<  3,
	usbman_xhci_port_status_and_control_flag_port_reset                 = 1 <<  4,
	usbman_xhci_port_status_and_control_flag_port_power                 = 1 <<  9,
	usbman_xhci_port_status_and_control_flag_link_state_write_strobe    = 1 << 16,
	usbman_xhci_port_status_and_control_flag_connect_status_change      = 1 << 17,
	usbman_xhci_port_status_and_control_flag_port_enabled_change        = 1 << 18,
	usbman_xhci_port_status_and_control_flag_warm_port_reset_change     = 1 << 19,
	usbman_xhci_port_status_and_control_flag_overcurrent_change         = 1 << 20,
	usbman_xhci_port_status_and_control_flag_port_reset_change          = 1 << 21,
	usbman_xhci_port_status_and_control_flag_port_link_state_change     = 1 << 22,
	usbman_xhci_port_status_and_control_flag_port_config_error_change   = 1 << 23,
	usbman_xhci_port_status_and_control_flag_cold_attach_status         = 1 << 24,
	usbman_xhci_port_status_and_control_flag_wake_on_connect_enable     = 1 << 25,
	usbman_xhci_port_status_and_control_flag_wake_on_disconnect_enable  = 1 << 26,
	usbman_xhci_port_status_and_control_flag_wake_on_overcurrent_enable = 1 << 27,
	usbman_xhci_port_status_and_control_flag_device_removable           = 1 << 30,
	usbman_xhci_port_status_and_control_flag_warm_port_reset            = 1 << 31,
};

#define FUSB_XHCI_PORT_STATUS_AND_CONTROL_WRITE_PRESERVE_MASK ( \
		((uint32_t)0x0f << 5)                                               | \
		(usbman_xhci_port_status_and_control_flag_port_power)                 | \
		((uint32_t)3 << 14)                                                 | \
		(usbman_xhci_port_status_and_control_flag_wake_on_connect_enable)     | \
		(usbman_xhci_port_status_and_control_flag_wake_on_disconnect_enable)  | \
		(usbman_xhci_port_status_and_control_flag_wake_on_overcurrent_enable)   \
	)

USBMAN_ALWAYS_INLINE
uint8_t usbman_xhci_port_get_link_state(volatile usbman_xhci_port_register_set_t* port_regs) {
	return (port_regs->status_and_control >> 5) & 0x0f;
};

USBMAN_ALWAYS_INLINE
void usbman_xhci_port_set_link_state(volatile usbman_xhci_port_register_set_t* port_regs, uint8_t link_state) {
	port_regs->status_and_control = ((port_regs->status_and_control & FUSB_XHCI_PORT_STATUS_AND_CONTROL_WRITE_PRESERVE_MASK) & ~(0x0f << 5)) | ((link_state & 0x0f) << 5);
};

USBMAN_ALWAYS_INLINE
uint8_t usbman_xhci_port_get_speed(volatile usbman_xhci_port_register_set_t* port_regs) {
	return (port_regs->status_and_control >> 10) & 0x0f;
};

USBMAN_ALWAYS_INLINE
uint8_t usbman_xhci_port_get_indicator(volatile usbman_xhci_port_register_set_t* port_regs) {
	return (port_regs->status_and_control >> 14) & 3;
};

USBMAN_ALWAYS_INLINE
void usbman_xhci_port_set_indicator(volatile usbman_xhci_port_register_set_t* port_regs, uint8_t indicator) {
	port_regs->status_and_control = ((port_regs->status_and_control & FUSB_XHCI_PORT_STATUS_AND_CONTROL_WRITE_PRESERVE_MASK) & ~(3 << 14)) | ((indicator & 3) << 5);
};

USBMAN_ALWAYS_INLINE
uint16_t usbman_xhci_port_get_link_error_count(volatile usbman_xhci_port_register_set_t* port_regs) {
	return port_regs->link_info & 0xffff;
};

USBMAN_ALWAYS_INLINE
uint8_t usbman_xhci_port_get_rx_lane_count(volatile usbman_xhci_port_register_set_t* port_regs) {
	return (port_regs->link_info >> 16) & 0x0f;
};

USBMAN_ALWAYS_INLINE
uint8_t usbman_xhci_port_get_tx_lane_count(volatile usbman_xhci_port_register_set_t* port_regs) {
	return (port_regs->link_info >> 20) & 0x0f;
};

USBMAN_PACKED_STRUCT(usbman_xhci_controller_operational_registers) {
	volatile uint32_t command;
	volatile uint32_t status;
	volatile uint32_t page_size;
	char reserved[8];
	volatile uint32_t device_notification_control;
	volatile uint64_t command_ring_control;
	char reserved2[16];
	volatile uint64_t device_context_base_address_array_pointer;
	volatile uint32_t configure;
	char reserved3[964];
	volatile usbman_xhci_port_register_set_t port_register_sets[];
};

USBMAN_PACKED_STRUCT(usbman_xhci_device_context_base_address_entry) {
	volatile uint64_t address;
};

USBMAN_PACKED_STRUCT(usbman_xhci_scratchpad_buffer_array_entry) {
	volatile uint64_t address;
};

USBMAN_ENUM(uint32_t, usbman_xhci_controller_command_flags) {
	usbman_xhci_controller_command_flag_run                            = 1 <<  0,
	usbman_xhci_controller_command_flag_host_controller_reset          = 1 <<  1,
	usbman_xhci_controller_command_flag_interrupter_enable             = 1 <<  2,
	usbman_xhci_controller_command_flag_host_system_error_enable       = 1 <<  3,
	usbman_xhci_controller_command_flag_light_host_controller_reset    = 1 <<  7,
	usbman_xhci_controller_command_flag_controller_save_state          = 1 <<  8,
	usbman_xhci_controller_command_flag_controller_restore_state       = 1 <<  9,
	usbman_xhci_controller_command_flag_enable_wrap_event              = 1 << 10,
	usbman_xhci_controller_command_flag_enable_u3_mfindex_stop         = 1 << 11,
	usbman_xhci_controller_command_flag_cem_enable                     = 1 << 13,
	usbman_xhci_controller_command_flag_extended_tbc_enable            = 1 << 14,
	usbman_xhci_controller_command_flag_extended_tbc_trb_status_enable = 1 << 15,
	usbman_xhci_controller_command_flag_vtio_enable                    = 1 << 16,
};

USBMAN_ENUM(uint32_t, usbman_xhci_controller_status_flags) {
	usbman_xhci_controller_status_flag_host_controller_halted = 1 <<  0,
	usbman_xhci_controller_status_flag_host_system_error      = 1 <<  2,
	usbman_xhci_controller_status_flag_event_interrupt        = 1 <<  3,
	usbman_xhci_controller_status_flag_port_change_detect     = 1 <<  4,
	usbman_xhci_controller_status_flag_save_state_status      = 1 <<  8,
	usbman_xhci_controller_status_flag_restore_state_status   = 1 <<  9,
	usbman_xhci_controller_status_flag_save_restore_error     = 1 << 10,
	usbman_xhci_controller_status_flag_controller_not_ready   = 1 << 11,
	usbman_xhci_controller_status_flag_host_controller_error  = 1 << 12,
};

USBMAN_ENUM(uint32_t, usbman_xhci_device_notification_control_flags) {
	usbman_xhci_device_notification_control_flag_wake_notification_enable = 1 << 1,
};

USBMAN_ENUM(uint64_t, usbman_xhci_command_ring_control_flags) {
	usbman_xhci_command_ring_control_flag_ring_cycle_state     = 1 << 0,
	usbman_xhci_command_ring_control_flag_command_stop         = 1 << 1,
	usbman_xhci_command_ring_control_flag_command_abort        = 1 << 2,
	usbman_xhci_command_ring_control_flag_command_ring_running = 1 << 3,
};

USBMAN_ALWAYS_INLINE
uint8_t usbman_xhci_controller_operational_registers_max_device_slots_enabled(volatile usbman_xhci_controller_operational_registers_t* op_regs) {
	return op_regs->configure & 0xff;
};

USBMAN_ENUM(uint32_t, usbman_xhci_configure_register_flags) {
	usbman_xhci_configure_register_flag_u3_entry_enable    = 1 << 8,
	usbman_xhci_configure_register_flag_config_info_enable = 1 << 9,
};

USBMAN_PACKED_STRUCT(usbman_xhci_interrupter_register_set) {
	volatile uint32_t management;
	volatile uint32_t moderation;
	volatile uint32_t event_ring_segment_table_size;
	char reserved[4];
	volatile uint64_t event_ring_segment_table_base_address;
	volatile uint64_t event_ring_dequeue_pointer;
};

USBMAN_ENUM(uint32_t, usbman_xhci_interrupter_management_flags) {
	usbman_xhci_interrupter_management_flag_pending = 1 << 0,
	usbman_xhci_interrupter_management_flag_enable  = 1 << 1,
};

USBMAN_PACKED_STRUCT(usbman_xhci_controller_runtime_registers) {
	volatile uint32_t microframe_index;
	char reserved[28];
	volatile usbman_xhci_interrupter_register_set_t interrupter_register_sets[1024];
};

USBMAN_ALWAYS_INLINE
uint32_t usbman_xhci_doorbell_make(uint8_t target, uint16_t stream_id) {
	return (uint32_t)target | ((uint32_t)stream_id << 16);
};

USBMAN_PACKED_STRUCT(usbman_xhci_trb) {
	volatile uint32_t parameters[2];
	volatile uint32_t status;
	volatile uint32_t control;
};

USBMAN_ENUM(uint8_t, usbman_xhci_trb_type) {
	usbman_xhci_trb_type_reserved = 0,

	usbman_xhci_trb_type_normal,
	usbman_xhci_trb_type_setup_stage,
	usbman_xhci_trb_type_data_stage,
	usbman_xhci_trb_type_status_stage,
	usbman_xhci_trb_type_isoch,
	usbman_xhci_trb_type_link,
	usbman_xhci_trb_type_event_data,
	usbman_xhci_trb_type_no_op,

	usbman_xhci_trb_type_enable_slot_command,
	usbman_xhci_trb_type_disable_slot_command,
	usbman_xhci_trb_type_address_device_command,
	usbman_xhci_trb_type_configure_endpoint_command,
	usbman_xhci_trb_type_evaluate_context_command,
	usbman_xhci_trb_type_reset_endpoint_command,
	usbman_xhci_trb_type_stop_endpoint_command,
	usbman_xhci_trb_type_set_tr_dequeue_pointer_command,
	usbman_xhci_trb_type_reset_device_command,
	usbman_xhci_trb_type_force_event_command,
	usbman_xhci_trb_type_negotiate_bandwidth_command,
	usbman_xhci_trb_type_set_latency_tolerance_value_command,
	usbman_xhci_trb_type_get_port_bandwidth_command,
	usbman_xhci_trb_type_force_header_command,
	usbman_xhci_trb_type_no_op_command,
	usbman_xhci_trb_type_get_extended_property_command,
	usbman_xhci_trb_type_set_extended_property_command,

	usbman_xhci_trb_type_transfer_event = 32,
	usbman_xhci_trb_type_command_completion_event,
	usbman_xhci_trb_type_port_status_change_event,
	usbman_xhci_trb_type_bandwidth_request_event,
	usbman_xhci_trb_type_doorbell_event,
	usbman_xhci_trb_type_host_controller_event,
	usbman_xhci_trb_type_device_notification_event,
	usbman_xhci_trb_type_microframe_index_wrap_event,

	usbman_xhci_trb_type_xxx_command_min = usbman_xhci_trb_type_enable_slot_command,
	usbman_xhci_trb_type_xxx_command_max = usbman_xhci_trb_type_set_extended_property_command,
	usbman_xhci_trb_type_xxx_event_min = usbman_xhci_trb_type_transfer_event,
	usbman_xhci_trb_type_xxx_event_max = usbman_xhci_trb_type_microframe_index_wrap_event,
};

USBMAN_ENUM(uint8_t, usbman_xhci_trb_completion_code) {
	usbman_xhci_trb_completion_code_invalid = 0,
	usbman_xhci_trb_completion_code_success,
	usbman_xhci_trb_completion_code_data_buffer_error,
	usbman_xhci_trb_completion_code_babble_detected_error,
	usbman_xhci_trb_completion_code_usb_transaction_error,
	usbman_xhci_trb_completion_code_trb_error,
	usbman_xhci_trb_completion_code_stall_error,
	usbman_xhci_trb_completion_code_resource_error,
	usbman_xhci_trb_completion_code_bandwidth_error,
	usbman_xhci_trb_completion_code_no_slots_available_error,
	usbman_xhci_trb_completion_code_invalid_stream_type_error,
	usbman_xhci_trb_completion_code_slot_not_enabled_error,
	usbman_xhci_trb_completion_code_endpoint_not_enabled_error,
	usbman_xhci_trb_completion_code_short_packet,
	usbman_xhci_trb_completion_code_ring_underrun,
	usbman_xhci_trb_completion_code_ring_overrun,
	usbman_xhci_trb_completion_code_vf_event_ring_full_error,
	usbman_xhci_trb_completion_code_parameter_error,
	usbman_xhci_trb_completion_code_bandwidth_overrun_error,
	usbman_xhci_trb_completion_code_context_state_error,
	usbman_xhci_trb_completion_code_no_ping_response_error,
	usbman_xhci_trb_completion_code_event_ring_full_error,
	usbman_xhci_trb_completion_code_incompatible_device_error,
	usbman_xhci_trb_completion_code_missed_service_error,
	usbman_xhci_trb_completion_code_command_ring_stopped,
	usbman_xhci_trb_completion_code_command_aborted,
	usbman_xhci_trb_completion_code_stopped,
	usbman_xhci_trb_completion_code_stopped_length_invalid,
	usbman_xhci_trb_completion_code_stopped_short_packet,
	usbman_xhci_trb_completion_code_max_exit_latency_too_large_error,

	usbman_xhci_trb_completion_code_isoch_buffer_overrun = 31,
	usbman_xhci_trb_completion_code_event_lost_error,
	usbman_xhci_trb_completion_code_undefined_error,
	usbman_xhci_trb_completion_code_invalid_stream_id_error,
	usbman_xhci_trb_completion_code_secondary_bandwidth_error,
	usbman_xhci_trb_completion_code_split_transaction_error,
};

USBMAN_ALWAYS_INLINE
usbman_xhci_trb_type_t usbman_xhci_trb_get_type(const volatile usbman_xhci_trb_t* trb) {
	return (trb->control >> 10) & 0x3f;
};

USBMAN_PACKED_STRUCT(usbman_xhci_erst_entry) {
	volatile uint32_t address_low;
	volatile uint32_t address_high;
	volatile uint32_t segment_size;
	volatile uint32_t reserved;
};

USBMAN_STRUCT(usbman_xhci_ring_common) {
	sys_mutex_t mutex;
	size_t entry_count;
	volatile void* physical_start;
	volatile usbman_xhci_trb_t* entries;
	volatile usbman_xhci_trb_t* dequeue;

	/**
	 * For consumer rings, this is the state of the cycle bit that indicates we own a TRB.
	 * For producer rings, this is the state of the cycle bit that we need to set on TRBs so consumers know they own those TRBs now.
	 */
	bool cycle_state;
};

typedef void (*usbman_xhci_producer_ring_callback_f)(void* context, const usbman_xhci_trb_t* consumed_trb, const usbman_xhci_trb_t* completion_trb);

USBMAN_STRUCT(usbman_xhci_producer_ring_callback_entry) {
	usbman_xhci_producer_ring_callback_f callback;
	void* context;
};

USBMAN_STRUCT(usbman_xhci_producer_ring) {
	usbman_xhci_ring_common_t common;
	volatile usbman_xhci_trb_t* enqueue;
	usbman_xhci_producer_ring_callback_entry_t* callbacks;
};

#define FUSB_XHCI_PRODUCER_RING_DEFAULT_ENTRY_COUNT 255

USBMAN_WUR ferr_t usbman_xhci_producer_ring_init(usbman_xhci_producer_ring_t* ring);
void usbman_xhci_producer_ring_destroy(usbman_xhci_producer_ring_t* ring);
USBMAN_WUR ferr_t usbman_xhci_producer_ring_produce(usbman_xhci_producer_ring_t* ring, const usbman_xhci_trb_t* trb, usbman_xhci_producer_ring_callback_f callback, void* context);
USBMAN_WUR ferr_t usbman_xhci_producer_ring_notify_consume(usbman_xhci_producer_ring_t* ring, const usbman_xhci_trb_t* completion_trb, usbman_xhci_trb_t* out_consumed_trb, usbman_xhci_producer_ring_callback_entry_t* out_callback_entry);

USBMAN_STRUCT(usbman_xhci_consumer_ring) {
	usbman_xhci_ring_common_t common;
	volatile void* physical_dequeue;
};

#define FUSB_XHCI_CONSUMER_RING_DEFAULT_ENTRY_COUNT 256

USBMAN_WUR ferr_t usbman_xhci_consumer_ring_init(usbman_xhci_consumer_ring_t* ring);
void usbman_xhci_consumer_ring_destroy(usbman_xhci_consumer_ring_t* ring);
USBMAN_WUR ferr_t usbman_xhci_consumer_ring_consume(usbman_xhci_consumer_ring_t* ring, usbman_xhci_trb_t* out_trb);

USBMAN_STRUCT_FWD(usbman_xhci_controller);

USBMAN_STRUCT(usbman_xhci_event_ring) {
	usbman_xhci_consumer_ring_t ring;
	volatile void* physical_table;
	volatile usbman_xhci_erst_entry_t* table;
	volatile uint64_t* dequeue_pointer;
	usbman_xhci_controller_t* controller;
};

USBMAN_WUR ferr_t usbman_xhci_event_ring_init(usbman_xhci_event_ring_t* event_ring, volatile uint64_t* dequeue_pointer, usbman_xhci_controller_t* controller);
USBMAN_WUR ferr_t usbman_xhci_event_ring_consume(usbman_xhci_event_ring_t* event_ring, usbman_xhci_trb_t* out_trb);
void usbman_xhci_event_ring_done_processing(usbman_xhci_event_ring_t* event_ring);
void usbman_xhci_event_ring_schedule_poll(usbman_xhci_event_ring_t* event_ring);

USBMAN_STRUCT(usbman_xhci_command_ring) {
	usbman_xhci_producer_ring_t ring;
	usbman_xhci_controller_t* controller;
};

USBMAN_WUR ferr_t usbman_xhci_command_ring_init(usbman_xhci_command_ring_t* command_ring, usbman_xhci_controller_t* controller);
USBMAN_WUR ferr_t usbman_xhci_command_ring_produce(usbman_xhci_command_ring_t* command_ring, const usbman_xhci_trb_t* trb, usbman_xhci_producer_ring_callback_f callback, void* context);
USBMAN_WUR ferr_t usbman_xhci_command_ring_notify_consume(usbman_xhci_command_ring_t* command_ring, const usbman_xhci_trb_t* completion_trb, usbman_xhci_trb_t* out_consumed_trb, usbman_xhci_producer_ring_callback_entry_t* out_callback_entry);

USBMAN_STRUCT(usbman_xhci_transfer_ring) {
	usbman_xhci_producer_ring_t ring;
	usbman_xhci_controller_t* controller;
	uint8_t slot_id;
	uint8_t dci;
	size_t available_count;

	size_t reserved_transaction_count;
	sys_mutex_t mutex;

	sys_semaphore_t transaction_reservation_semaphore;
};

// TODO: transfer rings should be able to have multiple reserved transactions at once, as many as the ring size allows.
//       right now, only one is allowed at a time.

USBMAN_WUR ferr_t usbman_xhci_transfer_ring_init(usbman_xhci_transfer_ring_t* transfer_ring, usbman_xhci_controller_t* controller, uint8_t slot_id, uint8_t dci);
void usbman_xhci_transfer_ring_destroy(usbman_xhci_transfer_ring_t* transfer_ring);
USBMAN_WUR ferr_t usbman_xhci_transfer_ring_produce(usbman_xhci_transfer_ring_t* transfer_ring, const usbman_xhci_trb_t* trb, usbman_xhci_producer_ring_callback_f callback, void* context);
USBMAN_WUR ferr_t usbman_xhci_transfer_ring_notify_consume(usbman_xhci_transfer_ring_t* transfer_ring, const usbman_xhci_trb_t* completion_trb, usbman_xhci_trb_t* out_consumed_trb, usbman_xhci_producer_ring_callback_entry_t* out_callback_entry);
USBMAN_WUR ferr_t usbman_xhci_transfer_ring_reserve_transaction(usbman_xhci_transfer_ring_t* transfer_ring, size_t trb_count, bool can_wait);

USBMAN_STRUCT(usbman_xhci_psi_array_entry) {
	usbman_speed_id_t standard_speed_id;
	uint64_t bitrate;
};

USBMAN_STRUCT(usbman_xhci_port_speed_entry) {
	uint8_t first_port_number;
	uint8_t last_port_number; // inclusive
	uint8_t major_version;
	uint8_t minor_version;

	// PSI values are 1-15 (0 is reserved), so this only needs 15 entries
	usbman_xhci_psi_array_entry_t map[15];
};

USBMAN_STRUCT(usbman_xhci_controller) {
	pci_device_t* device;
	usbman_controller_t* controller;
	size_t bar0_size;
	volatile usbman_xhci_controller_capability_registers_t* capability_registers;
	volatile usbman_xhci_controller_operational_registers_t* operational_registers;
	volatile usbman_xhci_controller_runtime_registers_t* runtime_registers;
	volatile uint32_t* extended_capabilities_base;
	volatile uint32_t* doorbell_array;
	volatile usbman_xhci_device_context_base_address_entry_t* device_context_base_address_array;
	usbman_xhci_command_ring_t command_ring;
	usbman_xhci_event_ring_t primary_event_ring;

	simple_ghmap_t ports;
	sys_mutex_t ports_mutex;
	uint8_t slots_to_ports[256];

	usbman_xhci_port_speed_entry_t* port_speed_map;
	size_t port_speed_map_entry_count;

	sys_semaphore_t init_semaphore;

	volatile uint64_t* scratchpad_buffer_array;
	void** virtual_scratchpad_buffer_array;
};

USBMAN_PACKED_STRUCT(usbman_xhci_context_slot) {
	volatile uint32_t fields[8];
};

USBMAN_PACKED_STRUCT(usbman_xhci_context_endpoint) {
	volatile uint32_t fields[8];
};

USBMAN_PACKED_STRUCT(usbman_xhci_context_device) {
	volatile usbman_xhci_context_slot_t slot;
	volatile usbman_xhci_context_endpoint_t endpoints[31];
};

USBMAN_PACKED_STRUCT(usbman_xhci_context_stream) {
	volatile uint32_t fields[4];
};

USBMAN_PACKED_STRUCT(usbman_xhci_context_input_control) {
	volatile uint32_t drop;
	volatile uint32_t add;
	volatile uint32_t reserved[5];
	volatile uint32_t configure;
};

USBMAN_PACKED_STRUCT(usbman_xhci_context_input) {
	volatile usbman_xhci_context_input_control_t control;
	volatile usbman_xhci_context_device_t device;
};

USBMAN_PACKED_STRUCT(usbman_xhci_context_port_bandwidth) {
	volatile uint32_t fields[4];
};

USBMAN_STRUCT_FWD(usbman_xhci_port);

USBMAN_STRUCT(usbman_xhci_endpoint) {
	usbman_xhci_port_t* port;
	uint8_t endpoint_id;
	usbman_xhci_transfer_ring_t default_control_transfer_ring;
};

USBMAN_STRUCT(usbman_xhci_port) {
	usbman_xhci_controller_t* controller;
	usbman_device_object_t* device;
	uint8_t port_number;
	uint8_t slot;
	uint8_t device_address;

	usbman_xhci_transfer_ring_t transfer_rings[31];

	usbman_xhci_context_device_t* output_device_context;

	void* temp;

	size_t max_packet_size;

	usbman_speed_id_t speed_id;
	uint64_t bitrate;
};

USBMAN_ENUM(uint8_t, usbman_xhci_endpoint_type) {
	usbman_xhci_endpoint_type_invalid = 0,
	usbman_xhci_endpoint_type_isoch_out,
	usbman_xhci_endpoint_type_bulk_out,
	usbman_xhci_endpoint_type_interrupt_out,
	usbman_xhci_endpoint_type_control,
	usbman_xhci_endpoint_type_isoch_in,
	usbman_xhci_endpoint_type_bulk_in,
	usbman_xhci_endpoint_type_interrupt_in,
};

USBMAN_ENUM(uint8_t, usbman_xhci_transfer_type) {
	usbman_xhci_transfer_type_no_data_stage = 0,
	usbman_xhci_transfer_type_out_data_stage = 2,
	usbman_xhci_transfer_type_in_data_stage = 3,
};

USBMAN_ENUM(uint32_t, usbman_xhci_transfer_flags) {
	usbman_xhci_transfer_flag_immediate_data = 1 << 6,
};

USBMAN_ENUM(uint8_t, usbman_xhci_xcap_id) {
	usbman_xhci_xcap_id_legacy_support = 1,
	usbman_xhci_xcap_id_supported_protocol,
	usbman_xhci_xcap_id_extended_power_management,
	usbman_xhci_xcap_id_io_virtualization,
	usbman_xhci_xcap_id_message_interrupt,
	usbman_xhci_xcap_id_local_memory,

	usbman_xhci_xcap_id_debug = 10,

	usbman_xhci_xcap_id_extended_message_interrupt = 17,
};

USBMAN_ALWAYS_INLINE
usbman_xhci_xcap_id_t usbman_xhci_xcap_get_id(volatile uint32_t* xcap_start) {
	return xcap_start[0] & 0xff;
};

USBMAN_ALWAYS_INLINE
volatile uint32_t* usbman_xhci_xcap_next(volatile uint32_t* xcap_start) {
	uint8_t ptr = (xcap_start[0] >> 8) & 0xff;
	if (ptr == 0) {
		return NULL;
	}
	return xcap_start + ptr;
};

USBMAN_PACKED_STRUCT(usbman_xhci_xcap_supported_protocol) {
	volatile uint32_t header;
	volatile uint32_t name_string;
	volatile uint32_t psic_and_compat_port_range;
	volatile uint32_t protocol_slot_type;
	volatile uint32_t psi_info[];
};

USBMAN_PACKED_STRUCT(usbman_xhci_xcap_legacy_support) {
	// NOTE: this XCAP is special because it supports 8-bit addressing (rather than 32-bit addressing)
	volatile uint16_t header;
	volatile uint8_t bios_semaphore;
	volatile uint8_t os_semaphore;
};

USBMAN_DECLARATIONS_END;

#endif // _USBMAN_DEV_XHCI_XHCI_PRIVATE_H_
