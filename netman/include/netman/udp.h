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

#ifndef _NETMAN_UDP_H_
#define _NETMAN_UDP_H_

#include <netman/base.h>
#include <netman/objects.h>

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

NETMAN_DECLARATIONS_BEGIN;

NETMAN_STRUCT_FWD(netman_ipv4_packet);
NETMAN_STRUCT_FWD(netman_device);
NETMAN_OBJECT_CLASS(udp_port);
NETMAN_OBJECT_CLASS(udp_packet);

typedef void (*netman_udp_port_handler_f)(void* context, netman_udp_port_t* port);

typedef uint16_t netman_udp_port_number_t;

#define NETMAN_UDP_PORT_NUMBER_DYNAMIC 0

void netman_udp_init(void);

NETMAN_WUR ferr_t netman_udp_handle_packet(netman_ipv4_packet_t* ip_packet);

NETMAN_WUR ferr_t netman_udp_register_port(netman_udp_port_number_t port_number, netman_udp_port_handler_f port_handler, void* context, netman_udp_port_t** out_port);
void netman_udp_unregister_port(netman_udp_port_t* port);

netman_udp_port_number_t netman_udp_port_number(netman_udp_port_t* port);

NETMAN_WUR size_t netman_udp_port_receive_packets(netman_udp_port_t* port, netman_udp_packet_t** out_packets, size_t array_space);

NETMAN_WUR ferr_t netman_udp_packet_create(netman_udp_packet_t** out_packet);

NETMAN_WUR ferr_t netman_udp_packet_map(netman_udp_packet_t* packet, void** out_mapped, size_t* out_length);
size_t netman_udp_packet_length(netman_udp_packet_t* packet);

NETMAN_WUR ferr_t netman_udp_packet_append(netman_udp_packet_t* packet, const void* data, size_t length, size_t* out_copied);
NETMAN_WUR ferr_t netman_udp_packet_extend(netman_udp_packet_t* packet, size_t length, bool zero, size_t* out_extended);

uint32_t netman_udp_packet_get_destination_address(netman_udp_packet_t* packet);
netman_udp_port_number_t netman_udp_packet_get_destination_port(netman_udp_packet_t* packet);

NETMAN_WUR ferr_t netman_udp_packet_set_destination_mac(netman_udp_packet_t* packet, const uint8_t* destination_mac);
NETMAN_WUR ferr_t netman_udp_packet_set_destination_address(netman_udp_packet_t* packet, uint32_t destination_address);
NETMAN_WUR ferr_t netman_udp_packet_set_destination_port(netman_udp_packet_t* packet, netman_udp_port_number_t port);

NETMAN_WUR ferr_t netman_udp_packet_get_source_mac(netman_udp_packet_t* packet, uint8_t* out_source_mac);
uint32_t netman_udp_packet_get_source_address(netman_udp_packet_t* packet);
netman_udp_port_number_t netman_udp_packet_get_source_port(netman_udp_packet_t* packet);

NETMAN_WUR ferr_t netman_udp_packet_set_source_port(netman_udp_packet_t* packet, netman_udp_port_number_t port);

// consumes the caller's reference on the packet
// the caller should be holding the only reference to the packet (as it will be invalidated after this call)
NETMAN_WUR ferr_t netman_udp_packet_transmit(netman_udp_packet_t* packet, netman_device_t* device);

NETMAN_DECLARATIONS_END;

#endif // _NETMAN_UDP_H_
