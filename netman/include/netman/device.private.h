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

#ifndef _NETMAN_DEVICE_PRIVATE_H_
#define _NETMAN_DEVICE_PRIVATE_H_

#include <stdbool.h>

#include <netman/device.h>
#include <libeve/libeve.h>

NETMAN_DECLARATIONS_BEGIN;

typedef void (*netman_device_tx_complete_callback_f)(void* data);

typedef void (*netman_device_rx_poll_f)(netman_device_t* device);
typedef void (*netman_device_tx_poll_f)(netman_device_t* device);
typedef ferr_t (*netman_device_tx_queue_f)(netman_device_t* device, void* data, size_t data_length, bool end_of_packet, size_t* out_queue_index);
typedef void (*netman_device_poll_return_f)(netman_device_t* device);

NETMAN_STRUCT(netman_device_methods) {
	netman_device_rx_poll_f rx_poll;
	netman_device_tx_poll_f tx_poll;
	netman_device_tx_queue_f tx_queue;
	netman_device_poll_return_f poll_return;
};

NETMAN_STRUCT(netman_device_packet_receive_hook) {
	netman_device_packet_receive_hook_f hook;
	void* data;
};

NETMAN_STRUCT(netman_device) {
	void* private_data;
	uint8_t mac_address[6];
	netman_device_packet_receive_hook_t rx_hooks[16];
	sys_mutex_t rx_hooks_lock;
	const netman_device_methods_t* methods;
	eve_loop_t* loop;

	bool rx_drop;
	netman_packet_t* rx_packet;

	netman_packet_t* tx_packet;
	size_t tx_packet_buffer_index;
	netman_packet_t** tx_pending;
	size_t tx_queue_size;
	sys_mutex_t tx_pending_lock;
};

NETMAN_WUR ferr_t netman_device_register(const uint8_t* mac_address, const netman_device_methods_t* methods, size_t tx_queue_size, netman_device_t** out_device);

void netman_device_rx_queue(netman_device_t* device, void* data, size_t data_length, bool end_of_packet, uint8_t checksum);

void netman_device_schedule_poll(netman_device_t* device, bool rx, bool tx);

void netman_device_tx_complete(netman_device_t* device, size_t queue_index);

NETMAN_DECLARATIONS_END;

#endif // _NETMAN_DEVICE_PRIVATE_H_
