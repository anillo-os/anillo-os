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

#include <netman/arp.h>
#include <netman/ip.h>
#include <netman/udp.h>
#include <netman/device.h>
#include <netman/tcp.h>
#include <libsimple/libsimple.h>

#define TEST_ADDR NETMAN_IPV4_ADDRESS(192, 168, 1, 113)

#define UDP_TESTING 0
#define TCP_TESTING 1

#if UDP_TESTING
static void netman_testing_udp_port_handler(void* context, netman_udp_port_t* port) {
	netman_udp_packet_t* packets[16];
	size_t count = 0;

	do {
		count = netman_udp_port_receive_packets(port, &packets[0], sizeof(packets) / sizeof(*packets));
		for (size_t i = 0; i < count; ++i) {
			netman_udp_packet_t* packet = packets[i];

			uint32_t source_ip = netman_udp_packet_get_source_address(packet);
			uint32_t dest_ip = netman_udp_packet_get_destination_address(packet);

			sys_console_log_f(
				"Got UDP packet: source=%u.%u.%u.%u:%u, dest=%u.%u.%u.%u:%u\n",
				NETMAN_IPV4_OCTET_A(source_ip),
				NETMAN_IPV4_OCTET_B(source_ip),
				NETMAN_IPV4_OCTET_C(source_ip),
				NETMAN_IPV4_OCTET_D(source_ip),
				netman_udp_packet_get_source_port(packet),
				NETMAN_IPV4_OCTET_A(dest_ip),
				NETMAN_IPV4_OCTET_B(dest_ip),
				NETMAN_IPV4_OCTET_C(dest_ip),
				NETMAN_IPV4_OCTET_D(dest_ip),
				netman_udp_packet_get_destination_port(packet)
			);

			netman_release(packet);
		}
	} while (count > 0);
};
#endif

#if TCP_TESTING
NETMAN_STRUCT(netman_testing_tcp_context) {
	uint8_t incoming_data[128];
	size_t incoming_data_length;
	size_t incoming_data_offset;

	uint8_t outgoing_data[128];
	size_t outgoing_data_length;
	size_t outgoing_data_offset;
};

static const char outgoing_data[] =
	"HTTP/1.0 200 OK\r\n"
	"Content-Type: text/html\r\n"
	"Content-Length: 46\r\n"
	"\r\n"
	"<html><body><p>Hello, world!</p></body></html>"
	;

static void netman_testing_tcp_client_handler(void* ctx, netman_tcp_connection_t* connection, netman_tcp_connection_events_t events) {
	netman_testing_tcp_context_t* context = ctx;
	uint8_t incoming_data[128];
	size_t received = 0;
	size_t sent = 0;

	sys_console_log_f("Client connection handler triggered with events=0x%02x\n", events);

	if ((events & netman_tcp_connection_event_connected) != 0) {
		sys_console_log("Client connection successfully established\n");
	}

	if ((events & netman_tcp_connection_event_reset) != 0) {
		sys_console_log("Client connection forcibly terminated (reset)\n");
	}

	if ((events & netman_tcp_connection_event_closed) == netman_tcp_connection_event_closed) {
		sys_console_log("Client connection fully closed; freeing context...\n");
		NETMAN_WUR_IGNORE(sys_mempool_free(context));
		return;
	} else if ((events & netman_tcp_connection_event_close_send) != 0) {
		sys_console_log("Client connection closed for sending; no more data may be sent\n");
	} else if ((events & netman_tcp_connection_event_close_receive) != 0) {
		sys_console_log("Client connection closed for receiving; no more data may be received\n");
	}

	if ((events & netman_tcp_connection_event_data_sent) != 0) {
		sys_console_log("Data has been sent and acknowledged by our client\n");
	}

	if ((events & netman_tcp_connection_event_data_received) != 0) {
		sys_console_log("Data has been received from our client\n");

		NETMAN_WUR_IGNORE(netman_tcp_connection_receive(connection, &context->incoming_data[context->incoming_data_offset], sizeof(context->incoming_data) - context->incoming_data_offset, &received));

		if (received > 0) {
			context->incoming_data_length += received;
			sys_console_log_f("Received data: %.*s\n", (int)received, &context->incoming_data[context->incoming_data_offset]);
			context->incoming_data_offset += received;
		}
	}

	if (context->outgoing_data_length - context->outgoing_data_offset > 0) {
		NETMAN_WUR_IGNORE(netman_tcp_connection_send(connection, &context->outgoing_data[context->outgoing_data_offset], context->outgoing_data_length - context->outgoing_data_offset, &sent));
		context->outgoing_data_offset += sent;
	}

	if ((events & netman_tcp_connection_event_close_receive) != 0) {
		sys_console_log("Client has closed their end; proceeding to close our end\n");
		netman_tcp_connection_close(connection);
		netman_release(connection);
	}
};

static void netman_testing_tcp_listener(void* ctx, netman_tcp_listener_t* listener) {
	netman_tcp_connection_t* connections[8];
	size_t accepted = 0;

	sys_console_log("Server listener handler triggered\n");

	accepted = netman_tcp_listener_accept(listener, connections, sizeof(connections) / sizeof(*connections));

	for (size_t i = 0; i < accepted; ++i) {
		netman_testing_tcp_context_t* context = NULL;

		sys_abort_status_log(sys_mempool_allocate(sizeof(*context), NULL, (void*)&context));

		simple_memset(context, 0, sizeof(*context));

		simple_memcpy(context->outgoing_data, outgoing_data, sizeof(outgoing_data));
		context->outgoing_data_length = sizeof(outgoing_data);

		sys_console_log_f("accepted connection = %p\n", connections[i]);

		netman_tcp_connection_set_handler(connections[i], netman_testing_tcp_client_handler, context);
		sys_abort_status_log(netman_tcp_connection_resume(connections[i]));
	}
};

static void netman_testing_tcp_connection_handler(void* ctx, netman_tcp_connection_t* connection, netman_tcp_connection_events_t events) {
	netman_testing_tcp_context_t* context = ctx;
	size_t received = 0;
	size_t sent = 0;

	sys_console_log_f("Connection handler triggered with events=0x%02x\n", events);

	if ((events & netman_tcp_connection_event_connected) != 0) {
		sys_console_log("Connection successfully established\n");
	}

	if ((events & netman_tcp_connection_event_reset) != 0) {
		sys_console_log("Connection forcibly terminated (reset)\n");
	}

	if ((events & netman_tcp_connection_event_closed) == netman_tcp_connection_event_closed) {
		sys_console_log("Connection fully closed; freeing context...\n");
		NETMAN_WUR_IGNORE(sys_mempool_free(context));
		return;
	} else if ((events & netman_tcp_connection_event_close_send) != 0) {
		sys_console_log("Connection closed for sending; no more data may be sent\n");
	} else if ((events & netman_tcp_connection_event_close_receive) != 0) {
		sys_console_log("Connection closed for receiving; no more data may be received\n");
	}

	if ((events & netman_tcp_connection_event_data_sent) != 0) {
		sys_console_log("Data has been sent and acknowledged by our peer\n");
	}

	if ((events & netman_tcp_connection_event_data_received) != 0) {
		sys_console_log("Data has been received from our peer\n");

		NETMAN_WUR_IGNORE(netman_tcp_connection_receive(connection, &context->incoming_data[context->incoming_data_offset], sizeof(context->incoming_data) - context->incoming_data_offset, &received));

		if (received > 0) {
			context->incoming_data_length += received;
			sys_console_log_f("Received data: %.*s\n", (int)received, &context->incoming_data[context->incoming_data_offset]);
			context->incoming_data_offset += received;
		}
	}

	if (context->outgoing_data_length - context->outgoing_data_offset > 0) {
		NETMAN_WUR_IGNORE(netman_tcp_connection_send(connection, &context->outgoing_data[context->outgoing_data_offset], context->outgoing_data_length - context->outgoing_data_offset, &sent));
		context->outgoing_data_offset += sent;
	}

	if ((events & netman_tcp_connection_event_data_received) != 0) {
		if (context->incoming_data_length == context->outgoing_data_length) {
			sys_console_log_f("Echo succeeded? %s\n", (simple_memcmp(context->incoming_data, context->outgoing_data, context->incoming_data_length) == 0) ? "YES" : "NO");
			netman_tcp_connection_close(connection);
			netman_release(connection);
		}
	}
};
#endif

void netman_testing(void) {
	ferr_t status = ferr_ok;

#if UDP_TESTING
	uint8_t mac[6] = {0};
	netman_udp_port_t* port = NULL;
	netman_udp_packet_t* packet = NULL;

	do {
		status = netman_arp_lookup_ipv4(NETMAN_IPV4_ADDRESS(192, 168, 1, 1), &mac[0]);
		if (status == ferr_ok) {
			break;
		}

		sys_console_log_f("waiting 1 sec\n");

		// wait 1 sec and then try again
		NETMAN_WUR_IGNORE(sys_thread_suspend_timeout(sys_thread_current(), 1000000000ull, sys_timeout_type_relative_ns_monotonic));
	} while (status != ferr_ok);

	sys_console_log_f("ARP lookup succeeded: 192.168.1.1 -> %02x:%02x:%02x:%02x:%02x:%02x\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

	sys_abort_status_log(netman_udp_register_port(NETMAN_UDP_PORT_NUMBER_DYNAMIC, netman_testing_udp_port_handler, NULL, &port));
	sys_console_log_f("Registered UDP listener on port %u\n", netman_udp_port_number(port));

	sys_abort_status_log(netman_udp_packet_create(&packet));

	sys_abort_status_log(netman_udp_packet_set_source_port(packet, netman_udp_port_number(port)));

	sys_abort_status_log(netman_udp_packet_append(packet, "HELLO WORLD", sizeof("HELLO WORLD") - 1, NULL));

	sys_abort_status_log(netman_udp_packet_set_destination_port(packet, 1234));

	do {
		status = netman_udp_packet_set_destination_address(packet, TEST_ADDR);
		if (status != ferr_ok && status != ferr_should_restart) {
			sys_abort_status_log(status);
		}
	} while (status == ferr_should_restart);

	sys_abort_status_log(netman_udp_packet_transmit(packet, netman_device_any()));
#endif

#if TCP_TESTING
	netman_tcp_listener_t* listener = NULL;

	sys_abort_status_log(netman_tcp_listen(80, netman_testing_tcp_listener, NULL, &listener));

	netman_tcp_connection_t* connection = NULL;
	netman_testing_tcp_context_t* context = NULL;

	sys_abort_status_log(sys_mempool_allocate(sizeof(*context), NULL, (void*)&context));

	simple_memset(context, 0, sizeof(*context));

	simple_memcpy(context->outgoing_data, "HELLO WORLD!", sizeof("HELLO WORLD!") - 1);
	context->outgoing_data_length = sizeof("HELLO WORLD!") - 1;

	do {
		status = netman_tcp_connect(TEST_ADDR, NULL, 8080, netman_testing_tcp_connection_handler, context, &connection);
		if (status != ferr_ok && status != ferr_should_restart) {
			sys_abort_status_log(status);
		}
	} while (status == ferr_should_restart);
#endif
};
