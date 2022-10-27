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

#ifndef _NETMAN_UDP_PRIVATE_H_
#define _NETMAN_UDP_PRIVATE_H_

#include <netman/udp.h>
#include <netman/objects.private.h>

NETMAN_DECLARATIONS_BEGIN;

NETMAN_PACKED_STRUCT(netman_udp_header) {
	uint16_t source_port;
	uint16_t destination_port;
	uint16_t length;
	uint16_t checksum;
};

NETMAN_STRUCT(netman_udp_packet_object) {
	netman_object_t object;
	uint16_t source_port;
	uint16_t destination_port;
	netman_ipv4_packet_t* packet;
};

NETMAN_STRUCT(netman_udp_port_object) {
	netman_object_t object;
	netman_udp_port_number_t port_number;
	netman_udp_port_handler_f handler;
	void* handler_context;

	sys_mutex_t rx_mutex;
	size_t rx_head;
	size_t rx_tail;
	size_t rx_ring_size;
	netman_udp_packet_t** rx_ring;
	bool rx_ring_full;
};

#define NETMAN_UDP_DYNAMIC_PORT_START 0xC000
#define NETMAN_UDP_DYNAMIC_PORT_COUNT 0x4000

#define NETMAN_UDP_DEFAULT_RING_SIZE 512

NETMAN_DECLARATIONS_END;

#endif // _NETMAN_UDP_PRIVATE_H_
