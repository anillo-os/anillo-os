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

#include <netman/udp.private.h>
#include <netman/ip.h>
#include <libsimple/libsimple.h>
#include <ferro/byteswap.h>
#include <libeve/libeve.h>

// only for checksum computation
#include <netman/ip.private.h>

static simple_ghmap_t ports;
static uint16_t next_dynamic_port = 0;
static sys_mutex_t ports_mutex = SYS_MUTEX_INIT;

void netman_udp_init(void) {
	sys_abort_status_log(simple_ghmap_init(&ports, 16, 0, simple_ghmap_allocate_sys_mempool, simple_ghmap_free_sys_mempool, NULL, NULL, NULL, NULL, NULL, NULL));
};

static void netman_udp_packet_destroy(netman_udp_packet_t* obj) {
	netman_udp_packet_object_t* packet = (void*)obj;
	if (packet->packet) {
		netman_ipv4_packet_destroy(packet->packet);
	}
	sys_object_destroy(obj);
};

static const netman_object_class_t udp_packet_object_class = {
	LIBSYS_OBJECT_CLASS_INTERFACE(NULL),
	.destroy = netman_udp_packet_destroy,
};

const netman_object_class_t* netman_object_class_udp_packet(void) {
	return &udp_packet_object_class;
};

static void netman_udp_port_destroy(netman_udp_port_t* obj) {
	netman_udp_port_object_t* port = (void*)obj;

	// unregister it (in case it hasn't been unregistered yet)
	netman_udp_unregister_port(obj);

	// first, tear down the receive queue so that no new datagrams are received into it

	eve_mutex_lock(&port->rx_mutex);

	if (port->rx_ring_full) {
		for (size_t i = 0; i < port->rx_ring_size; ++i) {
			netman_udp_packet_destroy(port->rx_ring[i]);
		}
	} else {
		for (size_t i = port->rx_head; i != port->rx_tail; i = (i + 1) % port->rx_ring_size) {
			netman_udp_packet_destroy(port->rx_ring[i]);
		}
	}

	port->rx_head = 0;
	port->rx_tail = 0;
	port->rx_ring_size = 0;
	port->rx_ring_full = true;

	sys_mutex_unlock(&port->rx_mutex);

	// we can now safely free the ring (no locks necessary)

	NETMAN_WUR_IGNORE(sys_mempool_free(port->rx_ring));
	port->rx_ring = NULL;

	sys_object_destroy(obj);
};

static const netman_object_class_t udp_port_object_class = {
	LIBSYS_OBJECT_CLASS_INTERFACE(NULL),
	.destroy = netman_udp_port_destroy,
};

const netman_object_class_t* netman_object_class_udp_port(void) {
	return &udp_port_object_class;
};

ferr_t netman_udp_handle_packet(netman_ipv4_packet_t* ip_packet) {
	ferr_t status = ferr_ok;
	netman_udp_header_t* header = NULL;
	netman_udp_port_object_t* port = NULL;
	netman_udp_port_t** port_ptr = NULL;
	netman_udp_packet_object_t* packet = NULL;

	if (netman_ipv4_packet_length(ip_packet) < sizeof(*header)) {
		status = ferr_too_small;
		goto out;
	}

	status = netman_ipv4_packet_map(ip_packet, (void*)&header, NULL);
	if (status != ferr_ok) {
		goto out;
	}

	eve_mutex_lock(&ports_mutex);

	status = simple_ghmap_lookup_h(&ports, ferro_byteswap_big_to_native_u16(header->destination_port), false, 0, NULL, (void*)&port_ptr, NULL);
	if (status != ferr_ok) {
		sys_mutex_unlock(&ports_mutex);
		goto out;
	}

	status = netman_retain(*port_ptr);
	if (status != ferr_ok) {
		sys_mutex_unlock(&ports_mutex);
		goto out;
	}

	port = (void*)*port_ptr;

	sys_mutex_unlock(&ports_mutex);

	// we allocate a packet only after checking if the port exists since, more often than not,
	// the reason we'll drop a packet is because nobody's listening on the given port.
	// however, we allocate it before checking if the queue is full because we don't want to
	// allocate memory while holding a lock.

	status = netman_object_new(&udp_packet_object_class, sizeof(*packet) - sizeof(packet->object), (void*)&packet);
	if (status != ferr_ok) {
		goto out;
	}

	packet->source_port = ferro_byteswap_big_to_native_u16(header->source_port);
	packet->destination_port = ferro_byteswap_big_to_native_u16(header->destination_port);
	packet->packet = ip_packet;

	eve_mutex_lock(&port->rx_mutex);

	if (port->rx_ring_full) {
		status = ferr_temporary_outage;
		sys_mutex_unlock(&port->rx_mutex);
		goto out;
	}

	port->rx_ring[port->rx_tail] = (void*)packet;
	port->rx_tail = (port->rx_tail + 1) % port->rx_ring_size;

	if (port->rx_head == port->rx_tail) {
		port->rx_ring_full = true;
	}

	sys_mutex_unlock(&port->rx_mutex);

	// notify the port handler
	//
	// we assume that the handler only performs quick, lightweight tasks
	// and schedules message processing to be done in some other context,
	// like a thread or a worker.
	port->handler(port->handler_context, (void*)port);

out:
	if (status != ferr_ok) {
		if (packet) {
			// note: we do NOT destroy the IPv4 packet because a non-ok return status
			//       tells our caller that we didn't handle this packet, so we don't own it.
			packet->packet = NULL;
			netman_release((void*)packet);
		}
	}
	if (port) {
		netman_release((void*)port);
	}
	return status;
};

ferr_t netman_udp_register_port(uint16_t port_number, netman_udp_port_handler_f port_handler, void* context, netman_udp_port_t** out_port) {
	ferr_t status = ferr_ok;
	bool created = false;
	netman_udp_port_object_t* port = NULL;
	netman_udp_port_t** port_ptr = NULL;

	if (!out_port || (port_number != NETMAN_UDP_PORT_NUMBER_DYNAMIC && port_number >= NETMAN_UDP_DYNAMIC_PORT_START)) {
		status = ferr_invalid_argument;
		goto out;
	}

	status = netman_object_new(&udp_port_object_class, sizeof(*port) - sizeof(port->object), (void*)&port);
	if (status != ferr_ok) {
		goto out;
	}

	sys_mutex_init(&port->rx_mutex);

	status = sys_mempool_allocate(sizeof(*port->rx_ring) * NETMAN_UDP_DEFAULT_RING_SIZE, NULL, (void*)&port->rx_ring);
	if (status != ferr_ok) {
		goto out;
	}

	eve_mutex_lock(&ports_mutex);

	if (port_number == NETMAN_UDP_PORT_NUMBER_DYNAMIC) {
		bool is_first = true;

		for (uint16_t i = next_dynamic_port; is_first || i != next_dynamic_port; (i = (i + 1) % NETMAN_UDP_DYNAMIC_PORT_COUNT)) {
			if (is_first) {
				is_first = false;
			}

			status = simple_ghmap_lookup_h(&ports, NETMAN_UDP_DYNAMIC_PORT_START + i, true, sizeof(*port_ptr), &created, (void*)&port_ptr, NULL);
			if (status != ferr_ok) {
				continue;
			}

			if (!created) {
				// dynamic ports are usually held for short periods of time and there is such a large
				// range that it is very likely another dynamic port will become available soon,
				// so this is a temporary outage
				status = ferr_temporary_outage;
				continue;
			}

			port_number = NETMAN_UDP_DYNAMIC_PORT_START + i;

			break;
		}

		if (status != ferr_ok) {
			sys_mutex_unlock(&ports_mutex);
			goto out;
		}

		next_dynamic_port = ((port_number - NETMAN_UDP_DYNAMIC_PORT_START) + 1) % NETMAN_UDP_DYNAMIC_PORT_COUNT;
	} else {
		status = simple_ghmap_lookup_h(&ports, port_number, true, sizeof(*port_ptr), &created, (void*)&port_ptr, NULL);

		if (!created) {
			sys_mutex_unlock(&ports_mutex);
			// non-dynamic ports are usually held for a relatively long time, so this is not a temporary outage
			status = ferr_resource_unavailable;
			goto out;
		}
	}

	port->port_number = port_number;
	port->handler = port_handler;
	port->handler_context = context;
	port->rx_ring_size = NETMAN_UDP_DEFAULT_RING_SIZE;

	*port_ptr = (void*)port;

	sys_mutex_unlock(&ports_mutex);

out:
	if (status == ferr_ok) {
		*out_port = (void*)port;
	} else {
		if (port) {
			netman_release((void*)port);
		}
	}
	return status;
};

void netman_udp_unregister_port(netman_udp_port_t* obj) {
	netman_udp_port_object_t* port = (void*)obj;
	netman_udp_port_t** port_ptr = NULL;

	// let's clear it from the global port table (if it's the currently registered port)

	eve_mutex_lock(&ports_mutex);

	if (simple_ghmap_lookup_h(&ports, port->port_number, false, 0, NULL, (void*)&port_ptr, NULL) == ferr_ok) {
		if (*port_ptr == obj) {
			sys_abort_status_log(simple_ghmap_clear_h(&ports, port->port_number));
		}
	}

	sys_mutex_unlock(&ports_mutex);
};

netman_udp_port_number_t netman_udp_port_number(netman_udp_port_t* obj) {
	netman_udp_port_object_t* port = (void*)obj;
	return port->port_number;
};

size_t netman_udp_port_receive_packets(netman_udp_port_t* obj, netman_udp_packet_t** out_packets, size_t array_space) {
	size_t received_count = 0;
	netman_udp_port_object_t* port = (void*)obj;

	if (array_space == 0) {
		goto out_unlocked;
	}

	eve_mutex_lock(&port->rx_mutex);

	if (port->rx_head == port->rx_tail && !port->rx_ring_full) {
		goto out;
	}

	while (received_count < array_space) {
		out_packets[received_count] = port->rx_ring[port->rx_head];
		++received_count;

		port->rx_head = (port->rx_head + 1) % port->rx_ring_size;

		if (port->rx_ring_full) {
			port->rx_ring_full = false;
		}

		if (port->rx_head == port->rx_tail) {
			break;
		}
	}

out:
	sys_mutex_unlock(&port->rx_mutex);
out_unlocked:
	return received_count;
};

ferr_t netman_udp_packet_create(netman_udp_packet_t** out_packet) {
	ferr_t status = ferr_ok;
	netman_udp_packet_object_t* packet = NULL;

	status = netman_object_new(&udp_packet_object_class, sizeof(*packet) - sizeof(packet->object), (void*)&packet);
	if (status != ferr_ok) {
		goto out;
	}

	status = netman_ipv4_packet_create(&packet->packet);
	if (status != ferr_ok) {
		goto out;
	}

	status = netman_ipv4_packet_set_protocol(packet->packet, netman_ipv4_protocol_type_udp);
	if (status != ferr_ok) {
		goto out;
	}

	status = netman_ipv4_packet_extend(packet->packet, sizeof(netman_udp_header_t), true, NULL);
	if (status != ferr_ok) {
		goto out;
	}

out:
	if (status == ferr_ok) {
		*out_packet = (void*)packet;
	} else {
		if (packet) {
			netman_release((void*)packet);
		}
	}
	return status;
};

ferr_t netman_udp_packet_map(netman_udp_packet_t* obj, void** out_mapped, size_t* out_length) {
	ferr_t status = ferr_ok;
	netman_udp_header_t* header = NULL;
	netman_udp_packet_object_t* packet = (void*)obj;

	if (!out_mapped) {
		status = ferr_invalid_argument;
		goto out;
	}

	status = netman_ipv4_packet_map(packet->packet, (void*)&header, NULL);
	if (status != ferr_ok) {
		goto out;
	}

out:
	if (status == ferr_ok) {
		*out_mapped = header + 1;
		if (out_length) {
			*out_length = netman_ipv4_packet_length(packet->packet) - sizeof(*header);
		}
	}

	return status;
};

size_t netman_udp_packet_length(netman_udp_packet_t* obj) {
	netman_udp_packet_object_t* packet = (void*)obj;
	return netman_ipv4_packet_length(packet->packet) - sizeof(netman_udp_header_t);
};

ferr_t netman_udp_packet_append(netman_udp_packet_t* obj, const void* data, size_t length, size_t* out_copied) {
	netman_udp_packet_object_t* packet = (void*)obj;
	return netman_ipv4_packet_append(packet->packet, data, length, out_copied);
};

ferr_t netman_udp_packet_extend(netman_udp_packet_t* obj, size_t length, bool zero, size_t* out_extended) {
	netman_udp_packet_object_t* packet = (void*)obj;
	return netman_ipv4_packet_extend(packet->packet, length, zero, out_extended);
};

uint32_t netman_udp_packet_get_destination_address(netman_udp_packet_t* obj) {
	netman_udp_packet_object_t* packet = (void*)obj;
	return netman_ipv4_packet_get_destination_address(packet->packet);
};

uint16_t netman_udp_packet_get_destination_port(netman_udp_packet_t* obj) {
	netman_udp_packet_object_t* packet = (void*)obj;
	return packet->destination_port;
};

ferr_t netman_udp_packet_set_destination_mac(netman_udp_packet_t* obj, const uint8_t* destination_mac) {
	netman_udp_packet_object_t* packet = (void*)obj;
	return netman_ipv4_packet_set_destination_mac(packet->packet, destination_mac);
};

ferr_t netman_udp_packet_set_destination_address(netman_udp_packet_t* obj, uint32_t destination_address) {
	netman_udp_packet_object_t* packet = (void*)obj;
	return netman_ipv4_packet_set_destination_address(packet->packet, destination_address);
};

ferr_t netman_udp_packet_set_destination_port(netman_udp_packet_t* obj, uint16_t port) {
	netman_udp_packet_object_t* packet = (void*)obj;
	packet->destination_port = port;
	return ferr_ok;
};

ferr_t netman_udp_packet_get_source_mac(netman_udp_packet_t* obj, uint8_t* out_source_mac) {
	netman_udp_packet_object_t* packet = (void*)obj;
	return netman_ipv4_packet_get_source_mac(packet->packet, out_source_mac);
};

uint32_t netman_udp_packet_get_source_address(netman_udp_packet_t* obj) {
	netman_udp_packet_object_t* packet = (void*)obj;
	return netman_ipv4_packet_get_source_address(packet->packet);
};

uint16_t netman_udp_packet_get_source_port(netman_udp_packet_t* obj) {
	netman_udp_packet_object_t* packet = (void*)obj;
	return packet->source_port;
};

ferr_t netman_udp_packet_set_source_port(netman_udp_packet_t* obj, uint16_t port) {
	netman_udp_packet_object_t* packet = (void*)obj;
	packet->source_port = port;
	return ferr_ok;
};

ferr_t netman_udp_packet_transmit(netman_udp_packet_t* obj, netman_device_t* device) {
	netman_udp_packet_object_t* packet = (void*)obj;
	netman_udp_header_t* header = NULL;
	ferr_t status = ferr_ok;
	netman_ipv4_checksum_state_t checksum_state;
	uint8_t buffer[4];

	status = netman_ipv4_packet_map(packet->packet, (void*)&header, NULL);
	if (status != ferr_ok) {
		goto out;
	}

	header->source_port = ferro_byteswap_native_to_big_u16(packet->source_port);
	header->destination_port = ferro_byteswap_native_to_big_u16(packet->destination_port);
	header->length = ferro_byteswap_native_to_big_u16(netman_ipv4_packet_length(packet->packet));
	header->checksum = 0;

	netman_ipv4_checksum_init(&checksum_state);

	// hard-coded for now
	*(uint32_t*)(&buffer[0]) = ferro_byteswap_native_to_big_u32(NETMAN_IPV4_STATIC_ADDRESS);
	netman_ipv4_checksum_add(&checksum_state, buffer, 4);

	*(uint32_t*)(&buffer[0]) = ferro_byteswap_native_to_big_u32(netman_ipv4_packet_get_destination_address(packet->packet));
	netman_ipv4_checksum_add(&checksum_state, buffer, 4);

	buffer[0] = 0;
	buffer[1] = netman_ipv4_protocol_type_udp;
	*(uint16_t*)(&buffer[2]) = header->length;
	netman_ipv4_checksum_add(&checksum_state, buffer, 4);

	netman_ipv4_checksum_add(&checksum_state, header, ferro_byteswap_big_to_native_u16(header->length));

	header->checksum = netman_ipv4_checksum_finish(&checksum_state);
	if (header->checksum == 0) {
		header->checksum = 0xffff;
	}

	status = netman_ipv4_packet_transmit(packet->packet, device);
	if (status != ferr_ok) {
		goto out;
	}

	// the IPv4 subsystem now owns the packet
	packet->packet = NULL;

	// the UDP packet has been transmitted successfully, so we can release it now
	netman_release((void*)packet);

out:
	return status;
};
