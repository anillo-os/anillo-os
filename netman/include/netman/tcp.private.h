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

#ifndef _NETMAN_TCP_PRIVATE_H_
#define _NETMAN_TCP_PRIVATE_H_

#include <netman/tcp.h>
#include <netman/objects.private.h>
#include <libeve/libeve.h>

NETMAN_DECLARATIONS_BEGIN;

NETMAN_ENUM(uint8_t, netman_tcp_control_bits) {
	netman_tcp_control_bit_fin = 1 << 0,
	netman_tcp_control_bit_syn = 1 << 1,
	netman_tcp_control_bit_rst = 1 << 2,
	netman_tcp_control_bit_psh = 1 << 3,
	netman_tcp_control_bit_ack = 1 << 4,
	netman_tcp_control_bit_urg = 1 << 5,
};

NETMAN_ENUM(uint8_t, netman_tcp_connection_state) {
	netman_tcp_connection_state_closed,
	netman_tcp_connection_state_closed_init,
	netman_tcp_connection_state_syn_sent,
	netman_tcp_connection_state_syn_received,
	netman_tcp_connection_state_established,
	netman_tcp_connection_state_fin_wait_1,
	netman_tcp_connection_state_fin_wait_2,
	netman_tcp_connection_state_close_wait,
	netman_tcp_connection_state_closing,
	netman_tcp_connection_state_last_ack,
	netman_tcp_connection_state_time_wait,
};

NETMAN_PACKED_STRUCT(netman_tcp_header) {
	uint16_t source_port;
	uint16_t destination_port;
	uint32_t sequence_number;
	uint32_t acknowledgement_number;
	uint8_t data_offset;
	uint8_t control_bits;
	uint16_t window;
	uint16_t checksum;
	uint16_t urgent_pointer;
};

NETMAN_STRUCT_FWD(netman_tcp_key);

// TODO: i should create a class for ring buffers in libsimple

NETMAN_STRUCT(netman_tcp_connection_object) {
	netman_object_t object;
	uint64_t internal_refcount;
	netman_tcp_connection_state_t state;
	netman_tcp_connection_handler_f handler;
	void* handler_context;

	const netman_tcp_key_t* key;

	// TODO: maybe these should be protected by a mutex or maybe they should be atomic?
	uint32_t rx_sequence_number;
	uint32_t tx_sequence_number;
	uint32_t tx_max_sequence_number;

	bool pending_ack_send;
	bool pending_reset;

	uint16_t tx_window;

	sys_mutex_t rx_mutex;
	size_t rx_head;
	size_t rx_tail;
	size_t rx_ring_size;
	void* rx_ring;
	bool rx_ring_full;

	sys_mutex_t tx_mutex;
	size_t tx_head;
	size_t tx_tail;
	size_t tx_ring_size;
	void* tx_ring;
	bool tx_ring_full;

	sys_mutex_t retransmit_mutex;
	eve_loop_work_id_t retransmit_work_id;

	netman_ipv4_packet_t* pending_packet;
};

#define NETMAN_TCP_DEFAULT_RX_RING_SIZE 512
#define NETMAN_TCP_DEFAULT_TX_RING_SIZE 512

NETMAN_STRUCT(netman_tcp_listener_object) {
	netman_object_t object;
	netman_tcp_port_number_t port_number;
	netman_tcp_listener_f listener;
	void* listener_context;

	sys_mutex_t pending_mutex;
	size_t pending_head;
	size_t pending_tail;
	size_t pending_ring_size;
	netman_ipv4_packet_t** pending_ring;
	bool pending_ring_full;
};

#define NETMAN_TCP_DEFAULT_PENDING_RING_SIZE 16

NETMAN_STRUCT(netman_tcp_key) {
	uint32_t peer_address;
	netman_tcp_port_number_t peer_port;

	uint32_t local_address;
	netman_tcp_port_number_t local_port;
};

#define NETMAN_TCP_DYNAMIC_PORT_START 0xC000
#define NETMAN_TCP_DYNAMIC_PORT_COUNT 0x4000

// 1500 MTU - 18 bytes of Ethernet framing - up to 60 bytes of IPv4 header - up to 60 bytes of TCP header = 1362
// round down to 1300 for good measure
#define NETMAN_TCP_MAX_TX_SEGMENT 1300

/**
 * Default retransmit timeout, in milliseconds.
 */
#define NETMAN_TCP_DEFAULT_RTO_MS 1000

/**
 * Default period of time to wait before cleaning up the connection, in milliseconds.
 */
#define NETMAN_TCP_DEFAULT_TIME_WAIT_MS 1000

NETMAN_DECLARATIONS_END;

#endif // _NETMAN_TCP_PRIVATE_H_
