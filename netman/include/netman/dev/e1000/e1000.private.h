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

#ifndef _NETMAN_DEV_E1000_E1000_PRIVATE_H_
#define _NETMAN_DEV_E1000_E1000_PRIVATE_H_

#include <netman/dev/e1000/e1000.h>
#include <libpci/libpci.h>
#include <netman/device.private.h>

#include <stdint.h>

#include <e1000/api.h>

NETMAN_DECLARATIONS_BEGIN;

NETMAN_ENUM(uint32_t, netman_e1000_register) {
	netman_e1000_register_control          = 0x0000 / sizeof(uint32_t),
	netman_e1000_register_eec              = 0x0010 / sizeof(uint32_t),
	netman_e1000_register_eerd             = 0x0014 / sizeof(uint32_t),
	netman_e1000_register_extended_control = 0x0018 / sizeof(uint32_t),
	netman_e1000_register_mdic             = 0x0020 / sizeof(uint32_t),
	netman_e1000_register_icr              = 0x00c0 / sizeof(uint32_t),
	netman_e1000_register_itr              = 0x00c4 / sizeof(uint32_t),
	netman_e1000_register_ims              = 0x00d0 / sizeof(uint32_t),
	netman_e1000_register_imc              = 0x00d8 / sizeof(uint32_t),
	netman_e1000_register_iam              = 0x00e0 / sizeof(uint32_t),
	netman_e1000_register_ivar             = 0x00e4 / sizeof(uint32_t),
	netman_e1000_register_rxcontrol        = 0x0100 / sizeof(uint32_t),
	netman_e1000_register_txcontrol        = 0x0400 / sizeof(uint32_t),
	netman_e1000_register_extcnf_ctrl      = 0x0f00 / sizeof(uint32_t),
	netman_e1000_register_rdbal            = 0x2800 / sizeof(uint32_t),
	netman_e1000_register_rdbah            = 0x2804 / sizeof(uint32_t),
	netman_e1000_register_rdlen            = 0x2808 / sizeof(uint32_t),
	netman_e1000_register_rdhead           = 0x2810 / sizeof(uint32_t),
	netman_e1000_register_rdtail           = 0x2818 / sizeof(uint32_t),
	netman_e1000_register_rdtimer          = 0x2820 / sizeof(uint32_t),
	netman_e1000_register_tdbal            = 0x3800 / sizeof(uint32_t),
	netman_e1000_register_tdbah            = 0x3804 / sizeof(uint32_t),
	netman_e1000_register_tdlen            = 0x3808 / sizeof(uint32_t),
	netman_e1000_register_tdhead           = 0x3810 / sizeof(uint32_t),
	netman_e1000_register_tdtail           = 0x3818 / sizeof(uint32_t),
	netman_e1000_register_tdtimer          = 0x3820 / sizeof(uint32_t),
	netman_e1000_register_mta_start        = 0x5200 / sizeof(uint32_t),
	netman_e1000_register_mta_end          = 0x5400 / sizeof(uint32_t),
	netman_e1000_register_rar_start        = 0x5400 / sizeof(uint32_t),
	netman_e1000_register_rar_end          = 0x5480 / sizeof(uint32_t),
};

NETMAN_ENUM(uint32_t, netman_e1000_control_bits) {
	netman_e1000_control_bit_full_duplex                 = 1 << 0,
	netman_e1000_control_bit_link_reset                  = 1 << 3,
	netman_e1000_control_bit_auto_speed_detection_enable = 1 << 5,
	netman_e1000_control_bit_set_link_up                 = 1 << 6,
	netman_e1000_control_bit_reset                       = 1 << 26,
	netman_e1000_control_bit_phy_reset                   = 1 << 31,
};

NETMAN_ENUM(uint32_t, netman_e1000_extended_control_bits) {
	netman_e1000_extended_control_bit_iam_enable = 1 << 27,
};

NETMAN_ENUM(uint8_t, netman_e1000_phy_selector) {
	netman_e1000_phy_selector_gigabit = 1,
	netman_e1000_phy_selector_pcie    = 2,
};

NETMAN_ENUM(uint32_t, netman_e1000_mdic_bits) {
	netman_e1000_mdic_bit_operation_read   = 2 << 26,
	netman_e1000_mdic_bit_operation_write  = 1 << 26,
	netman_e1000_mdic_bit_ready            = 1 << 28,
	netman_e1000_mdic_bit_interrupt_enable = 1 << 29,
	netman_e1000_mdic_bit_error            = 1 << 30,
};

NETMAN_ENUM(uint32_t, netman_e1000_rxcontrol_bits) {
	netman_e1000_rxcontrol_bit_receive_enable               = 1 << 1,
	netman_e1000_rxcontrol_bit_store_bad_packets            = 1 << 2,
	netman_e1000_rxcontrol_bit_unicast_promiscuous_enable   = 1 << 3,
	netman_e1000_rxcontrol_bit_multicast_promiscuous_enable = 1 << 4,
	netman_e1000_rxcontrol_bit_long_packet_enable           = 1 << 5,
	netman_e1000_rxcontrol_bit_minimum_threshold_half       = 0 << 8,
	netman_e1000_rxcontrol_bit_descriptor_type_legacy       = 0 << 10,
	netman_e1000_rxcontrol_bit_broadcast_accept_mode        = 1 << 15,
	netman_e1000_rxcontrol_bit_buffer_size_extension        = 1 << 25,
	netman_e1000_rxcontrol_bit_strip_ethernet_crc           = 1 << 26,
};

NETMAN_ENUM(uint32_t, netman_e1000_rxcontrol_descriptor_size) {
	netman_e1000_rxcontrol_descriptor_size_256   = 3 << 16,
	netman_e1000_rxcontrol_descriptor_size_512   = 2 << 16,
	netman_e1000_rxcontrol_descriptor_size_1024  = 1 << 16,
	netman_e1000_rxcontrol_descriptor_size_2048  = 0 << 16,
	netman_e1000_rxcontrol_descriptor_size_4096  = netman_e1000_rxcontrol_descriptor_size_256 | netman_e1000_rxcontrol_bit_buffer_size_extension,
	netman_e1000_rxcontrol_descriptor_size_8192  = netman_e1000_rxcontrol_descriptor_size_512 | netman_e1000_rxcontrol_bit_buffer_size_extension,
	netman_e1000_rxcontrol_descriptor_size_16384 = netman_e1000_rxcontrol_descriptor_size_1024 | netman_e1000_rxcontrol_bit_buffer_size_extension,
};

NETMAN_ENUM(uint8_t, netman_e1000_rx_status) {
	netman_e1000_rx_status_ready         = 1 << 0,
	netman_e1000_rx_status_end_of_packet = 1 << 1,
};

NETMAN_ENUM(uint8_t, netman_e1000_tx_status) {
	netman_e1000_tx_status_ready = 1 << 0,
};

NETMAN_ENUM(uint8_t, netman_e1000_tx_command) {
	netman_e1000_tx_command_end_of_packet          = 1 << 0,
	netman_e1000_tx_command_insert_fcs             = 1 << 1,
	netman_e1000_tx_command_insert_checksum        = 1 << 2,
	netman_e1000_tx_command_report_status          = 1 << 3,
	netman_e1000_tx_command_descriptor_extension   = 1 << 5,
	netman_e1000_tx_command_vlan_packet_enable     = 1 << 6,
	netman_e1000_tx_command_interrupt_delay_enable = 1 << 7,
};

NETMAN_ENUM(uint32_t, netman_e1000_txcontrol_bits) {
	netman_e1000_txcontrol_bit_transmit_enable   = 1 << 1,
	netman_e1000_txcontrol_bit_pad_short_packets = 1 << 3,
};

NETMAN_ENUM(uint32_t, netman_e1000_interrupt_cause) {
	netman_e1000_interrupt_cause_tx_writeback         = 1 << 0,
	netman_e1000_interrupt_cause_tx_queue_empty       = 1 << 1,
	netman_e1000_interrupt_cause_link_status_change   = 1 << 2,
	netman_e1000_interrupt_cause_rx_minimum_threshold = 1 << 4,
	netman_e1000_interrupt_cause_rx_overrun           = 1 << 6,
	netman_e1000_interrupt_cause_rx_timer             = 1 << 7,
	netman_e1000_interrupt_cause_mdio_access_complete = 1 << 9,
	netman_e1000_interrupt_cause_tx_low_threshold     = 1 << 15,
	netman_e1000_interrupt_cause_small_packet_receive = 1 << 16,
	netman_e1000_interrupt_cause_ack                  = 1 << 17,
	netman_e1000_interrupt_cause_mng                  = 1 << 18,
	netman_e1000_interrupt_cause_rx_queue_0           = 1 << 20,
	netman_e1000_interrupt_cause_rx_queue_1           = 1 << 21,
	netman_e1000_interrupt_cause_tx_queue_0           = 1 << 22,
	netman_e1000_interrupt_cause_tx_queue_1           = 1 << 23,
	netman_e1000_interrupt_cause_other                = 1 << 24,
	netman_e1000_interrupt_cause_interrupt_asserted   = 1 << 31,

	netman_e1000_interrupt_cause_all_known            =
		netman_e1000_interrupt_cause_tx_writeback         |
		netman_e1000_interrupt_cause_tx_queue_empty       |
		netman_e1000_interrupt_cause_link_status_change   |
		netman_e1000_interrupt_cause_rx_minimum_threshold |
		netman_e1000_interrupt_cause_rx_overrun           |
		netman_e1000_interrupt_cause_rx_timer             |
		netman_e1000_interrupt_cause_mdio_access_complete |
		netman_e1000_interrupt_cause_tx_low_threshold     |
		netman_e1000_interrupt_cause_small_packet_receive |
		netman_e1000_interrupt_cause_ack                  |
		netman_e1000_interrupt_cause_mng                  |
		netman_e1000_interrupt_cause_rx_queue_0           |
		netman_e1000_interrupt_cause_rx_queue_1           |
		netman_e1000_interrupt_cause_tx_queue_0           |
		netman_e1000_interrupt_cause_tx_queue_1           |
		netman_e1000_interrupt_cause_other                |
		0,
};

NETMAN_PACKED_STRUCT(netman_e1000_rx_descriptor) {
	volatile uint64_t address;
	volatile uint16_t length;
	volatile uint16_t checksum;
	volatile uint8_t status;
	volatile uint8_t errors;
	volatile uint16_t vlan_tag;
};

NETMAN_PACKED_STRUCT(netman_e1000_tx_descriptor) {
	volatile uint64_t address;
	volatile uint16_t length;
	volatile uint8_t checksum_offset;
	volatile uint8_t command;
	volatile uint8_t status_and_extended_command;
	volatile uint8_t checksum_start;
	volatile uint16_t vlan;
};

NETMAN_ENUM(uint8_t, netman_e1000_rom_access_type) {
	netman_e1000_rom_access_type_eeprom_hw_lock,
	netman_e1000_rom_access_type_eeprom_register_lock,
	netman_e1000_rom_access_type_flash,
};

NETMAN_STRUCT(netman_e1000_model_info) {
	uint16_t vendor_id;
	uint16_t product_id;
};

NETMAN_STRUCT(netman_e1000) {
	pci_device_t* device;
	netman_device_t* net_device;
	volatile uint32_t* bar0;
	size_t bar0_size;

	size_t rx_ring_count;
	volatile void* rx_ring_phys;
	volatile netman_e1000_rx_descriptor_t* rx_ring;
	void** rx_ring_virt_addrs;

	size_t tx_ring_count;
	volatile void* tx_ring_phys;
	volatile netman_e1000_tx_descriptor_t* tx_ring;
	void** tx_ring_virt_addrs;
	size_t tx_oldest_pending_index;

	const netman_e1000_model_info_t* model_info;
	volatile uint32_t* flash_bar;
	size_t flash_bar_size;

	struct e1000_hw library_handle;

	bool needs_management_passthrough;
	bool has_amt;
};

#define NETMAN_INTEL_E1000E_RX_RING_COUNT_DEFAULT 512
#define NETMAN_INTEL_E1000E_RX_RING_BUFFER_SIZE 4096
#define NETMAN_INTEL_E1000E_RX_RING_BUFFER_SIZE_CTRL netman_e1000_rxcontrol_descriptor_size_4096

#define NETMAN_INTEL_E1000E_TX_RING_COUNT_DEFAULT 512

void netman_e1000_rx_enable(netman_e1000_t* nic);
void netman_e1000_rx_disable(netman_e1000_t* nic);

void netman_e1000_tx_enable(netman_e1000_t* nic);
void netman_e1000_tx_disable(netman_e1000_t* nic);

uint32_t netman_e1000_bar0_read_32(netman_e1000_t* nic, size_t offset);
void netman_e1000_bar0_write_32(netman_e1000_t* nic, size_t offset, uint32_t value);

uint32_t netman_e1000_flash_read_32(netman_e1000_t* nic, size_t offset);
void netman_e1000_flash_write_32(netman_e1000_t* nic, size_t offset, uint32_t value);

uint16_t netman_e1000_flash_read_16(netman_e1000_t* nic, size_t offset);
void netman_e1000_flash_write_16(netman_e1000_t* nic, size_t offset, uint16_t value);

NETMAN_DECLARATIONS_END;

#endif // _NETMAN_DEV_E1000_E1000_PRIVATE_H_
