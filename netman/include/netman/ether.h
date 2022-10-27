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

#ifndef _NETMAN_ETHER_H_
#define _NETMAN_ETHER_H_

#include <netman/base.h>

#include <stddef.h>
#include <stdint.h>

NETMAN_DECLARATIONS_BEGIN;

NETMAN_STRUCT_FWD(netman_packet);

NETMAN_ENUM(uint16_t, netman_ether_packet_type) {
	netman_ether_packet_type_ipv4 = 0x0800,
	netman_ether_packet_type_arp  = 0x0806,
};

NETMAN_PACKED_STRUCT(netman_ether_packet) {
	uint8_t destination[6];
	uint8_t source[6];
	uint16_t ethertype;
	char payload[];
};

void netman_ether_init(void);

NETMAN_WUR ferr_t netman_ether_packet_write_header(netman_packet_t* packet, const uint8_t* source_mac, const uint8_t* destination_mac, netman_ether_packet_type_t packet_type, size_t* out_payload_offset);

NETMAN_WUR ferr_t netman_ether_packet_set_source_mac(netman_packet_t* packet, const uint8_t* source_mac);
NETMAN_WUR ferr_t netman_ether_packet_set_destination_mac(netman_packet_t* packet, const uint8_t* destination_mac);

NETMAN_WUR ferr_t netman_ether_packet_get_source_mac(netman_packet_t* packet, uint8_t* out_source_mac);
NETMAN_WUR ferr_t netman_ether_packet_get_destination_mac(netman_packet_t* packet, uint8_t* out_destination_mac);

NETMAN_ALWAYS_INLINE
size_t netman_ether_required_packet_size(size_t payload_size) {
	return payload_size + 14;
};

static const uint8_t netman_ether_broadcast_address[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

NETMAN_DECLARATIONS_END;

#endif // _NETMAN_ETHER_H_
