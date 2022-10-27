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

#ifndef _NETMAN_IP_H_
#define _NETMAN_IP_H_

#include <netman/base.h>

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

NETMAN_DECLARATIONS_BEGIN;

NETMAN_STRUCT_FWD(netman_packet);
NETMAN_STRUCT_FWD(netman_ipv4_packet);
NETMAN_STRUCT_FWD(netman_device);

NETMAN_ENUM(uint8_t, netman_ipv4_protocol_type) {
	netman_ipv4_protocol_type_icmp = 1,
	netman_ipv4_protocol_type_tcp = 6,
	netman_ipv4_protocol_type_udp = 17,
};

void netman_ipv4_init(void);

#define NETMAN_IPV4_ADDRESS(a, b, c, d) (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | ((uint32_t)(d) << 0))

#define NETMAN_IPV4_OCTET_A(address) ((uint8_t)(((uint32_t)(address) >> 24) & 0xff))
#define NETMAN_IPV4_OCTET_B(address) ((uint8_t)(((uint32_t)(address) >> 16) & 0xff))
#define NETMAN_IPV4_OCTET_C(address) ((uint8_t)(((uint32_t)(address) >>  8) & 0xff))
#define NETMAN_IPV4_OCTET_D(address) ((uint8_t)(((uint32_t)(address) >>  0) & 0xff))

// static address for testing
#define NETMAN_IPV4_STATIC_ADDRESS NETMAN_IPV4_ADDRESS(192, 168, 1, 10)

#define NETMAN_IPV4_LOCAL_BROADCAST_ADDRESS NETMAN_IPV4_ADDRESS(255, 255, 255, 255)

NETMAN_WUR ferr_t netman_ipv4_handle_packet(netman_packet_t* packet, size_t payload_offset);

NETMAN_WUR ferr_t netman_ipv4_packet_create(netman_ipv4_packet_t** out_ip_packet);

NETMAN_WUR ferr_t netman_ipv4_packet_set_destination_mac(netman_ipv4_packet_t* ip_packet, const uint8_t* destination_mac);
NETMAN_WUR ferr_t netman_ipv4_packet_set_destination_address(netman_ipv4_packet_t* ip_packet, uint32_t destination_address);
NETMAN_WUR ferr_t netman_ipv4_packet_set_protocol(netman_ipv4_packet_t* ip_packet, netman_ipv4_protocol_type_t protocol);

NETMAN_WUR ferr_t netman_ipv4_packet_map(netman_ipv4_packet_t* ip_packet, void** out_mapped, size_t* out_length);

size_t netman_ipv4_packet_length(netman_ipv4_packet_t* ip_packet);

NETMAN_WUR ferr_t netman_ipv4_packet_append(netman_ipv4_packet_t* ip_packet, const void* data, size_t length, size_t* out_copied);
NETMAN_WUR ferr_t netman_ipv4_packet_extend(netman_ipv4_packet_t* ip_packet, size_t length, bool zero, size_t* out_extended);

NETMAN_WUR ferr_t netman_ipv4_packet_transmit(netman_ipv4_packet_t* ip_packet, netman_device_t* device);

NETMAN_WUR ferr_t netman_ipv4_packet_get_source_mac(netman_ipv4_packet_t* ip_packet, uint8_t* out_source_mac);
uint32_t netman_ipv4_packet_get_source_address(netman_ipv4_packet_t* ip_packet);
uint32_t netman_ipv4_packet_get_destination_address(netman_ipv4_packet_t* ip_packet);

void netman_ipv4_packet_destroy(netman_ipv4_packet_t* ip_packet);

NETMAN_WUR ferr_t netman_ipv4_packet_extract_data(netman_ipv4_packet_t* ip_packet, void** out_data, size_t* out_length);

NETMAN_DECLARATIONS_END;

#endif // _NETMAN_IP_H_
