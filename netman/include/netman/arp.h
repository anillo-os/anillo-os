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

#ifndef _NETMAN_ARP_H_
#define _NETMAN_ARP_H_

#include <netman/base.h>

#include <stddef.h>
#include <stdint.h>

#include <netman/ether.h>

NETMAN_DECLARATIONS_BEGIN;

NETMAN_STRUCT_FWD(netman_packet);

void netman_arp_init(void);

NETMAN_WUR ferr_t netman_arp_lookup(netman_ether_packet_type_t protocol_type, const uint8_t* protocol_address, size_t protocol_address_length, uint8_t* out_mac);
NETMAN_WUR ferr_t netman_arp_lookup_ipv4(uint32_t ip_address, uint8_t* out_mac);

NETMAN_WUR ferr_t netman_arp_handle_packet(netman_packet_t* packet, size_t payload_offset);

NETMAN_WUR ferr_t netman_arp_register(netman_ether_packet_type_t protocol_type, const uint8_t* protocol_address, size_t protocol_address_length, const uint8_t* mac);
NETMAN_WUR ferr_t netman_arp_register_ipv4(uint32_t ip_address, const uint8_t* mac);

NETMAN_DECLARATIONS_END;

#endif // _NETMAN_ARP_H_
