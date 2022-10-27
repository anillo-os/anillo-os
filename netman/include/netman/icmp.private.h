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

#ifndef _NETMAN_ICMP_PRIVATE_H_
#define _NETMAN_ICMP_PRIVATE_H_

#include <netman/icmp.h>

NETMAN_DECLARATIONS_BEGIN;

NETMAN_ENUM(uint8_t, netman_icmp_type) {
	netman_icmp_type_echo_reply = 0,
	netman_icmp_type_echo_request = 8,
};

NETMAN_PACKED_STRUCT(netman_icmp_header) {
	netman_icmp_type_t type;
	uint8_t code;
	uint16_t checksum;
};

NETMAN_PACKED_STRUCT(netman_icmp_echo_header) {
	netman_icmp_header_t header;
	uint16_t identifier;
	uint16_t sequence_number;
};

NETMAN_DECLARATIONS_END;

#endif // _NETMAN_ICMP_PRIVATE_H_
