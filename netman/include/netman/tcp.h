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

#ifndef _NETMAN_TCP_H_
#define _NETMAN_TCP_H_

#include <netman/base.h>
#include <netman/objects.h>

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

NETMAN_DECLARATIONS_BEGIN;

NETMAN_STRUCT_FWD(netman_ipv4_packet);
NETMAN_OBJECT_CLASS(tcp_connection);
NETMAN_OBJECT_CLASS(tcp_listener);

NETMAN_ENUM(uint8_t, netman_tcp_connection_events) {
	netman_tcp_connection_event_none          = 0,

	/**
	 * Data is available to be received from the connection.
	 */
	netman_tcp_connection_event_data_received = 1 << 0,

	/**
	 * Data has been sent and acknowledged by our peer, freeing up space in the send buffer.
	 */
	netman_tcp_connection_event_data_sent     = 1 << 1,

	/**
	 * The send side of the connection has been closed, meaning we can no longer send data.
	 */
	netman_tcp_connection_event_close_send    = 1 << 2,

	/**
	 * The receive side of the connection has been closed, meaning we can no longer receive data.
	 */
	netman_tcp_connection_event_close_receive = 1 << 3,

	/**
	 * The connection has been successfully established.
	 */
	netman_tcp_connection_event_connected     = 1 << 4,

	/**
	 * The connection has been reset.
	 *
	 * All data in the send and receive buffers has been discarded and the connection has been fully terminated.
	 *
	 * A ::netman_tcp_connection_event_closed event is always sent along with this event.
	 */
	netman_tcp_connection_event_reset         = 1 << 5,

	/**
	 * The connection has been closed for both sending and receiving. It is now fully terminated.
	 */
	netman_tcp_connection_event_closed        = netman_tcp_connection_event_close_send | netman_tcp_connection_event_close_receive,
};

typedef uint16_t netman_tcp_port_number_t;
typedef void (*netman_tcp_listener_f)(void* context, netman_tcp_listener_t* listener);
typedef void (*netman_tcp_connection_handler_f)(void* context, netman_tcp_connection_t* connection, netman_tcp_connection_events_t events);

void netman_tcp_init(void);
NETMAN_WUR ferr_t netman_tcp_handle_packet(netman_ipv4_packet_t* ip_packet);

NETMAN_WUR ferr_t netman_tcp_listen(netman_tcp_port_number_t port_number, netman_tcp_listener_f listener, void* context, netman_tcp_listener_t** out_listener);
size_t netman_tcp_listener_accept(netman_tcp_listener_t* listener, netman_tcp_connection_t** out_connections, size_t array_space);

NETMAN_WUR ferr_t netman_tcp_connect(uint32_t address, const uint8_t* mac, netman_tcp_port_number_t port, netman_tcp_connection_handler_f handler, void* context, netman_tcp_connection_t** out_connection);
NETMAN_WUR ferr_t netman_tcp_connection_receive(netman_tcp_connection_t* connection, void* buffer, size_t buffer_size, size_t* out_received);
NETMAN_WUR ferr_t netman_tcp_connection_send(netman_tcp_connection_t* connection, const void* buffer, size_t buffer_length, size_t* out_sent);
void netman_tcp_connection_close(netman_tcp_connection_t* connection);

void netman_tcp_connection_set_handler(netman_tcp_connection_t* connection, netman_tcp_connection_handler_f handler, void* context);
NETMAN_WUR ferr_t netman_tcp_connection_resume(netman_tcp_connection_t* connection);

NETMAN_DECLARATIONS_END;

#endif // _NETMAN_TCP_H_
