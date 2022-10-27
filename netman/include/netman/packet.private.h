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

#ifndef _NETMAN_PACKET_PRIVATE_H_
#define _NETMAN_PACKET_PRIVATE_H_

#include <netman/packet.h>
#include <netman/device.h>

NETMAN_DECLARATIONS_BEGIN;

NETMAN_STRUCT(netman_packet_buffer) {
	void* address;
	size_t length;
};

NETMAN_STRUCT(netman_packet) {
	netman_packet_buffer_t* buffers;
	size_t buffer_count;
	size_t buffer_allocated_count;
	void* last_page_mapping;
	void* persistent_mapping;
	size_t total_length;
	netman_device_transmit_packet_callback_f tx_callback;
	void* tx_callback_data;
};

NETMAN_DECLARATIONS_END;

#endif // _NETMAN_PACKET_PRIVATE_H_
