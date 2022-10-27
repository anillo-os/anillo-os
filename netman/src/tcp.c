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

#include <netman/tcp.private.h>
#include <libsimple/libsimple.h>
#include <ferro/byteswap.h>
#include <netman/ip.h>
#include <netman/device.h>

// for checksumming
#include <netman/ip.private.h>

// FIXME: there might be a race between the retransmit worker running and the destruction of the connection

#ifndef NETMAN_TCP_DEBUG
	#define NETMAN_TCP_DEBUG 1
#endif

#if NETMAN_TCP_DEBUG
#define netman_tcp_debug(msg) sys_console_log("TCP: " msg "\n")
#define netman_tcp_debugf(format, ...) sys_console_log_f("TCP: " format "\n", ## __VA_ARGS__)
#else
#define netman_tcp_debug(msg)
#define netman_tcp_debugf(format, ...)
#endif

static simple_ghmap_t connection_table;
static uint16_t next_dynamic_port = 0;
static sys_mutex_t connection_table_mutex = SYS_MUTEX_INIT;

static simple_ghmap_t port_table;
static sys_mutex_t port_table_mutex = SYS_MUTEX_INIT;

void netman_tcp_init(void) {
	sys_abort_status_log(simple_ghmap_init(&connection_table, 16, 0, simple_ghmap_allocate_sys_mempool, simple_ghmap_free_sys_mempool, simple_ghmap_hash_data, simple_ghmap_compares_equal_data, NULL, NULL, NULL, NULL));
	sys_abort_status_log(simple_ghmap_init(&port_table, 16, 0, simple_ghmap_allocate_sys_mempool, simple_ghmap_free_sys_mempool, NULL, NULL, NULL, NULL, NULL, NULL));
};

static void netman_tcp_connection_destroy_internal(netman_tcp_connection_object_t* connection) {
	netman_tcp_debug("Releasing and cleaning up connection");

	sys_mutex_lock(&connection->rx_mutex);

	connection->rx_head = 0;
	connection->rx_tail = 0;
	connection->rx_ring_size = 0;
	connection->rx_ring_full = true;

	sys_mutex_unlock(&connection->rx_mutex);

	NETMAN_WUR_IGNORE(sys_mempool_free(connection->rx_ring));
	connection->rx_ring = NULL;

	sys_mutex_lock(&connection->tx_mutex);

	connection->tx_head = 0;
	connection->tx_tail = 0;
	connection->tx_ring_size = 0;
	connection->tx_ring_full = true;

	sys_mutex_unlock(&connection->tx_mutex);

	NETMAN_WUR_IGNORE(sys_mempool_free(connection->tx_ring));
	connection->tx_ring = NULL;

	sys_mutex_lock(&connection->retransmit_mutex);
	if (connection->retransmit_work_id != eve_loop_work_id_invalid) {
		NETMAN_WUR_IGNORE(eve_loop_cancel(eve_loop_get_main(), connection->retransmit_work_id));
	}
	sys_mutex_unlock(&connection->retransmit_mutex);

	if (connection->pending_packet) {
		netman_ipv4_packet_destroy(connection->pending_packet);
		connection->pending_packet = NULL;
	}

	sys_mutex_lock(&connection_table_mutex);
	NETMAN_WUR_IGNORE(simple_ghmap_clear(&connection_table, connection->key, sizeof(*connection->key)));
	sys_mutex_unlock(&connection_table_mutex);
};

static ferr_t netman_tcp_connection_retain_internal(netman_tcp_connection_object_t* connection) {
	uint64_t old_value = __atomic_load_n(&connection->internal_refcount, __ATOMIC_RELAXED);

	do {
		if (old_value == 0) {
			return ferr_permanent_outage;
		}
	} while (!__atomic_compare_exchange_n(&connection->internal_refcount, &old_value, old_value + 1, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED));

	return ferr_ok;
};

static void netman_tcp_connection_release_internal(netman_tcp_connection_object_t* connection) {
	uint64_t old_value = __atomic_load_n(&connection->internal_refcount, __ATOMIC_RELAXED);

	do {
		if (old_value == 0) {
			return;
		}
	} while (!__atomic_compare_exchange_n(&connection->internal_refcount, &old_value, old_value - 1, false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED));

	if (old_value != 1) {
		return;
	}

	netman_tcp_connection_destroy_internal(connection);
};

static void netman_tcp_connection_destroy(netman_tcp_connection_t* obj) {
	netman_tcp_connection_object_t* connection = (void*)obj;
	// close our end if we hadn't already
	// TODO: maybe we should send a reset instead?
	netman_tcp_connection_close((void*)connection);
	netman_tcp_connection_release_internal(connection);
};

static void netman_tcp_listener_destroy(netman_tcp_listener_t* obj) {
	netman_tcp_listener_object_t* listener = (void*)obj;

	sys_mutex_lock(&listener->pending_mutex);

	if (listener->pending_ring_full) {
		for (size_t i = 0; i < listener->pending_ring_size; ++i) {
			netman_ipv4_packet_destroy(listener->pending_ring[i]);
		}
	} else {
		for (size_t i = listener->pending_head; i != listener->pending_tail; i = (i + 1) % listener->pending_ring_size) {
			netman_ipv4_packet_destroy(listener->pending_ring[i]);
		}
	}

	listener->pending_head = 0;
	listener->pending_tail = 0;
	listener->pending_ring_size = 0;
	listener->pending_ring_full = true;

	sys_mutex_unlock(&listener->pending_mutex);

	NETMAN_WUR_IGNORE(sys_mempool_free(listener->pending_ring));
	listener->pending_ring = NULL;

	sys_mutex_lock(&port_table_mutex);
	NETMAN_WUR_IGNORE(simple_ghmap_clear_h(&port_table, listener->port_number));
	sys_mutex_unlock(&port_table_mutex);
};

static const netman_object_class_t tcp_connection_object_class = {
	LIBSYS_OBJECT_CLASS_INTERFACE(NULL),
	.destroy = netman_tcp_connection_destroy,
};

static const netman_object_class_t tcp_listener_object_class = {
	LIBSYS_OBJECT_CLASS_INTERFACE(NULL),
	.destroy = netman_tcp_listener_destroy,
};

static void netman_tcp_connection_try_send(netman_tcp_connection_object_t* connection);

static void netman_tcp_connection_schedule_retransmit(netman_tcp_connection_object_t* connection) {
	sys_mutex_lock(&connection->retransmit_mutex);
	netman_tcp_debugf("scheduling retransmission...");
	if (connection->retransmit_work_id == eve_loop_work_id_invalid) {
		NETMAN_WUR_IGNORE(eve_loop_schedule(eve_loop_get_main(), (void*)netman_tcp_connection_try_send, connection, NETMAN_TCP_DEFAULT_RTO_MS * 1000000, sys_timeout_type_relative_ns_monotonic, &connection->retransmit_work_id));
	}
	sys_mutex_unlock(&connection->retransmit_mutex);
};

static void netman_tcp_connection_try_send(netman_tcp_connection_object_t* connection) {
	netman_ipv4_packet_t* packet = NULL;
	ferr_t status = ferr_ok;
	netman_tcp_header_t* header = NULL;
	size_t tx_avail_data_length = 0;
	size_t tx_avail_data_end_length = 0;
	size_t tx_length_this_packet = 0;
	size_t tx_length_curr = 0;
	size_t rx_space = 0;
	netman_ipv4_checksum_state_t checksum_state;
	uint8_t checksum_buffer[4];
	bool needs_retransmit = false;

	sys_mutex_lock(&connection->retransmit_mutex);
	netman_tcp_debugf("trying to send");
	if (connection->retransmit_work_id != eve_loop_work_id_invalid) {
		NETMAN_WUR_IGNORE(eve_loop_cancel(eve_loop_get_main(), connection->retransmit_work_id));
		connection->retransmit_work_id = eve_loop_work_id_invalid;
	}
	sys_mutex_unlock(&connection->retransmit_mutex);

	if (connection->state == netman_tcp_connection_state_closed) {
		netman_tcp_debug("not sending packets while connection is closed");
		goto out;
	}

	status = netman_ipv4_packet_create(&packet);
	if (status != ferr_ok) {
		goto out;
	}

	status = netman_ipv4_packet_set_protocol(packet, netman_ipv4_protocol_type_tcp);
	if (status != ferr_ok) {
		goto out;
	}

	status = netman_ipv4_packet_set_destination_address(packet, connection->key->peer_address);
	if (status != ferr_ok) {
		goto out;
	}

	// check how big our receive window is right now.
	// it's okay if this info becomes outdated by the time we send the message.
	sys_mutex_lock(&connection->rx_mutex);
	rx_space = (connection->rx_tail >= connection->rx_head) ? (connection->rx_ring_size - (connection->rx_tail - connection->rx_head)) : (connection->rx_head - connection->rx_tail);
	sys_mutex_unlock(&connection->rx_mutex);

	// FIXME: we shouldn't be doing memory allocation while holding a lock.
	//        the problem is that we don't want someone else to change the TX buffer
	//        while we allocate a buffer for everything we're going to send.
	//        setting a flag like "doing transmit" is appealing, but it complicates
	//        the situation in netman_tcp_connection_handle_packet() for acknowledgements.

	sys_mutex_lock(&connection->tx_mutex);

	if (connection->tx_ring_full) {
		tx_avail_data_length = connection->tx_ring_size;
	} else if (connection->tx_tail != connection->tx_head) {
		tx_avail_data_length = (connection->tx_tail >= connection->tx_head) ? (connection->tx_tail - connection->tx_head) : (connection->tx_ring_size - (connection->tx_head - connection->tx_tail));
		tx_avail_data_end_length = (connection->tx_tail < connection->tx_head) ? (connection->tx_ring_size - connection->tx_head) : 0;
	}

	tx_length_this_packet = (tx_avail_data_length > NETMAN_TCP_MAX_TX_SEGMENT) ? NETMAN_TCP_MAX_TX_SEGMENT : tx_avail_data_length;
	tx_length_curr = (tx_avail_data_end_length < tx_length_this_packet) ? tx_avail_data_end_length : tx_length_this_packet;

	if (connection->pending_reset) {
		tx_length_this_packet = 0;
		tx_length_curr = 0;
	}

	if (
		tx_length_this_packet == 0 &&
		!connection->pending_ack_send &&
		!connection->pending_reset &&
		connection->state != netman_tcp_connection_state_syn_sent &&
		connection->state != netman_tcp_connection_state_fin_wait_1 &&
		connection->state != netman_tcp_connection_state_last_ack
	) {
		// we actually don't need to send any data or acknowledgements
		netman_ipv4_packet_destroy(packet);
		netman_tcp_debugf("no need to send packet");
		sys_mutex_unlock(&connection->tx_mutex);
		goto out;
	}

	status = netman_ipv4_packet_extend(packet, sizeof(*header) + tx_length_this_packet, false, NULL);
	if (status != ferr_ok) {
		sys_mutex_unlock(&connection->tx_mutex);
		goto out;
	}

	status = netman_ipv4_packet_map(packet, (void*)&header, NULL);
	if (status != ferr_ok) {
		sys_mutex_unlock(&connection->tx_mutex);
		goto out;
	}

	header->source_port = ferro_byteswap_native_to_big_u16(connection->key->local_port);
	header->destination_port = ferro_byteswap_native_to_big_u16(connection->key->peer_port);
	header->sequence_number = ferro_byteswap_native_to_big_u32(connection->tx_sequence_number);
	header->acknowledgement_number = ferro_byteswap_native_to_big_u32(connection->rx_sequence_number);
	header->data_offset = (uint8_t)(sizeof(*header) / 4) << 4;
	header->control_bits = 0;
	header->window = ferro_byteswap_native_to_big_u16(rx_space);
	header->checksum = 0;
	header->urgent_pointer = 0;

	if (connection->pending_reset) {
		header->control_bits = netman_tcp_control_bit_rst;
	} else {
		if (connection->state == netman_tcp_connection_state_syn_sent || connection->state == netman_tcp_connection_state_syn_received) {
			header->control_bits |= netman_tcp_control_bit_syn;
		} else if (
			(
				connection->state == netman_tcp_connection_state_fin_wait_1 ||
				connection->state == netman_tcp_connection_state_last_ack
			) &&
			connection->tx_sequence_number == connection->tx_max_sequence_number
		) {
			header->control_bits |= netman_tcp_control_bit_fin;
		}

		if (connection->state != netman_tcp_connection_state_syn_sent) {
			header->control_bits |= netman_tcp_control_bit_ack;
		}

		if (tx_length_this_packet > 0) {
			header->control_bits |= netman_tcp_control_bit_psh;
		}
	}

	if (tx_length_this_packet > 0) {
		size_t data_offset = sizeof(*header);

		size_t curr_head = connection->tx_head;

		simple_memcpy((char*)header + data_offset, &((char*)connection->tx_ring)[curr_head], tx_length_curr);
		curr_head = (curr_head + tx_length_curr) % connection->tx_ring_size;
		data_offset += tx_length_curr;
		tx_length_this_packet -= tx_length_curr;
		tx_avail_data_length -= tx_length_curr;

		tx_length_curr = (tx_avail_data_length < tx_length_this_packet) ? tx_avail_data_length : tx_length_this_packet;

		simple_memcpy((char*)header + data_offset, &((char*)connection->tx_ring)[curr_head], tx_length_curr);
		curr_head = (curr_head + tx_length_curr) % connection->tx_ring_size;
		data_offset += tx_length_curr;
		tx_length_this_packet -= tx_length_curr;
		tx_avail_data_length -= tx_length_curr;
	}

	sys_mutex_unlock(&connection->tx_mutex);

	netman_ipv4_checksum_init(&checksum_state);

	*(uint32_t*)(&checksum_buffer[0]) = ferro_byteswap_native_to_big_u32(connection->key->local_address);
	netman_ipv4_checksum_add(&checksum_state, checksum_buffer, 4);

	*(uint32_t*)(&checksum_buffer[0]) = ferro_byteswap_native_to_big_u32(connection->key->peer_address);
	netman_ipv4_checksum_add(&checksum_state, checksum_buffer, 4);

	checksum_buffer[0] = 0;
	checksum_buffer[1] = netman_ipv4_protocol_type_tcp;
	*(uint16_t*)(&checksum_buffer[2]) = ferro_byteswap_native_to_big_u16(netman_ipv4_packet_length(packet));
	netman_ipv4_checksum_add(&checksum_state, checksum_buffer, 4);

	netman_ipv4_checksum_add(&checksum_state, header, netman_ipv4_packet_length(packet));

	header->checksum = netman_ipv4_checksum_finish(&checksum_state);

	status = netman_ipv4_packet_transmit(packet, netman_device_any());
	if (status != ferr_ok) {
		goto out;
	}

	// the device now owns the packet
	packet = NULL;

	if (connection->pending_reset) {
		connection->pending_reset = false;
		connection->tx_sequence_number = 0;

		needs_retransmit = true;
	}

out:
	if (status != ferr_ok) {
		if (packet) {
			netman_ipv4_packet_destroy(packet);
		}

		needs_retransmit = true;
	} else {
		connection->pending_ack_send = false;
	}

	if (needs_retransmit) {
		netman_tcp_connection_schedule_retransmit((void*)connection);
	}
};

static void netman_tcp_send_reset(uint32_t local_address, uint16_t local_port, uint32_t peer_address, uint16_t peer_port, uint32_t sequence_number, bool send_with_ack) {
	ferr_t status = ferr_ok;
	netman_ipv4_packet_t* packet = NULL;
	netman_tcp_header_t* header = NULL;
	netman_ipv4_checksum_state_t checksum_state;
	uint8_t checksum_buffer[4];

	status = netman_ipv4_packet_create(&packet);
	if (status != ferr_ok) {
		goto out;
	}

	status = netman_ipv4_packet_set_protocol(packet, netman_ipv4_protocol_type_tcp);
	if (status != ferr_ok) {
		goto out;
	}

	status = netman_ipv4_packet_set_destination_address(packet, peer_address);
	if (status != ferr_ok) {
		goto out;
	}

	status = netman_ipv4_packet_extend(packet, sizeof(*header), false, NULL);
	if (status != ferr_ok) {
		goto out;
	}

	status = netman_ipv4_packet_map(packet, (void*)&header, NULL);
	if (status != ferr_ok) {
		goto out;
	}

	header->source_port = ferro_byteswap_native_to_big_u16(local_port);
	header->destination_port = ferro_byteswap_native_to_big_u16(peer_port);
	header->sequence_number = ferro_byteswap_native_to_big_u32(send_with_ack ? 0 : sequence_number);
	header->acknowledgement_number = ferro_byteswap_native_to_big_u32(send_with_ack ? sequence_number : 0);
	header->data_offset = (uint8_t)(sizeof(*header) / 4) << 4;
	header->control_bits = netman_tcp_control_bit_rst | (send_with_ack ? netman_tcp_control_bit_ack : 0);
	header->window = ferro_byteswap_native_to_big_u16(0);
	header->checksum = 0;
	header->urgent_pointer = 0;

	netman_ipv4_checksum_init(&checksum_state);

	*(uint32_t*)(&checksum_buffer[0]) = ferro_byteswap_native_to_big_u32(local_address);
	netman_ipv4_checksum_add(&checksum_state, checksum_buffer, 4);

	*(uint32_t*)(&checksum_buffer[0]) = ferro_byteswap_native_to_big_u32(peer_address);
	netman_ipv4_checksum_add(&checksum_state, checksum_buffer, 4);

	checksum_buffer[0] = 0;
	checksum_buffer[1] = netman_ipv4_protocol_type_tcp;
	*(uint16_t*)(&checksum_buffer[2]) = ferro_byteswap_native_to_big_u16(netman_ipv4_packet_length(packet));
	netman_ipv4_checksum_add(&checksum_state, checksum_buffer, 4);

	netman_ipv4_checksum_add(&checksum_state, header, netman_ipv4_packet_length(packet));

	header->checksum = netman_ipv4_checksum_finish(&checksum_state);

	status = netman_ipv4_packet_transmit(packet, netman_device_any());
	if (status != ferr_ok) {
		goto out;
	}

	// the device now owns the packet
	packet = NULL;

out:
	if (status != ferr_ok) {
		if (packet) {
			netman_ipv4_packet_destroy(packet);
		}
	}
};

static void netman_tcp_send_reset_detect_type(uint32_t local_address, uint16_t local_port, uint32_t peer_address, uint16_t peer_port, netman_tcp_header_t* header, size_t packet_length) {
	size_t data_offset = (size_t)(header->data_offset >> 4) * 4;
	size_t data_length = packet_length - data_offset;
	uint32_t seq = ferro_byteswap_big_to_native_u32(header->sequence_number);

	uint32_t reset_seq = seq + data_length;
	bool send_with_ack = true;

	if ((header->control_bits & netman_tcp_control_bit_ack) != 0) {
		reset_seq = ferro_byteswap_big_to_native_u32(header->acknowledgement_number);
		send_with_ack = false;
	}

	netman_tcp_send_reset(local_address, local_port, peer_address, peer_port, reset_seq, send_with_ack);
};

static void netman_tcp_connection_timed_wait_expire(netman_tcp_connection_object_t* connection) {
	connection->state = netman_tcp_connection_state_closed;

	// release the timed wait's reference
	netman_tcp_connection_release_internal(connection);

	// release the connection's life reference
	// (held only while the connection is alive/open)
	netman_tcp_connection_release_internal(connection);
};

static void netman_tcp_connection_handle_packet(netman_tcp_connection_object_t* connection, netman_ipv4_packet_t* ip_packet) {
	netman_tcp_header_t* header = NULL;
	netman_tcp_connection_events_t events = 0;

	if (netman_ipv4_packet_map(ip_packet, (void*)&header, NULL) != ferr_ok) {
		goto out;
	}

	netman_tcp_debugf("received packet on connection %p; current state = %u", connection, connection->state);

	// TODO: support incoming data while still doing the handshake

	if (connection->state == netman_tcp_connection_state_closed_init) {
		if (header->control_bits != netman_tcp_control_bit_syn) {
			// this should've already been caught in functions that call us
			netman_tcp_debug("impossible error: SYN bit not set on incoming connection");
			goto out;
		}

		connection->state = netman_tcp_connection_state_syn_received;
		connection->rx_sequence_number = ferro_byteswap_big_to_native_u32(header->sequence_number) + 1;
		connection->pending_ack_send = true;

		netman_tcp_connection_try_send(connection);
	} else if (connection->state == netman_tcp_connection_state_syn_sent) {
		uint32_t seq = ferro_byteswap_big_to_native_u32(header->sequence_number);
		uint32_t ack = ferro_byteswap_big_to_native_u32(header->acknowledgement_number);
		bool has_valid_ack = false;

		if ((header->control_bits & netman_tcp_control_bit_ack) != 0) {
			if (ack == connection->tx_sequence_number + 1) {
				has_valid_ack = true;
			} else {
				// we had a previous connection with them but lost it and they don't know this yet;
				// let's tell them.
				netman_tcp_debug("connection need to be reset");

				netman_tcp_send_reset(connection->key->local_address, connection->key->local_port, connection->key->peer_address, connection->key->peer_port, ack, false);

				// now schedule a retransmission to send the SYN packet again
				netman_tcp_connection_schedule_retransmit(connection);

				goto out;
			}
		}

		if ((header->control_bits & netman_tcp_control_bit_rst) != 0) {
			if (has_valid_ack) {
				// peer wants us to reset our connection
				netman_tcp_debug("peer asked us to reset our connection");
				connection->state = netman_tcp_connection_state_closed;

				// release the connection's life reference
				// (held only while the connection is alive/open)
				netman_tcp_connection_release_internal(connection);

				events |= netman_tcp_connection_event_closed | netman_tcp_connection_event_reset;
			} else {
				// just drop the packet
				netman_tcp_debug("ignoring unexpected/invalid RST packet");
			}

			goto out;
		}

		if ((header->control_bits & netman_tcp_control_bit_syn) == 0) {
			netman_tcp_debug("bad packet: no SYN bit");
			goto out;
		}

		if (!has_valid_ack) {
			// our peer simultaneously opened a connection with us.
			// enter SYN_RECEIVED
			netman_tcp_debug("simultaneous open occurred; switching to SYN_RECEIVED");
			connection->state = netman_tcp_connection_state_syn_received;
			connection->rx_sequence_number = ferro_byteswap_big_to_native_u32(header->sequence_number) + 1;
			connection->pending_ack_send = true;

			netman_tcp_connection_try_send(connection);
		} else {
			connection->state = netman_tcp_connection_state_established;
			connection->rx_sequence_number = seq + 1;
			connection->tx_sequence_number = ack;
			connection->tx_max_sequence_number = connection->tx_sequence_number;
			connection->pending_ack_send = true;

			netman_tcp_connection_try_send(connection);

			events |= netman_tcp_connection_event_connected;
		}
	} else if (connection->state == netman_tcp_connection_state_syn_received) {
		uint32_t seq = ferro_byteswap_big_to_native_u32(header->sequence_number);
		uint32_t ack = ferro_byteswap_big_to_native_u32(header->acknowledgement_number);

		if (seq != connection->rx_sequence_number) {
			// bad packet

			if ((header->control_bits & netman_tcp_control_bit_rst) != 0) {
				// ignore the packet
				netman_tcp_debug("ignoring out-of-order packet with RST bit set");
			} else {
				// let's tell them what we were actually expecting
				netman_tcp_debug("received out-of-order packet; informing peer with ACK");
				connection->pending_ack_send = true;

				netman_tcp_connection_try_send(connection);
			}
			
			goto out;
		}

		if ((header->control_bits & netman_tcp_control_bit_rst) != 0) {
			netman_tcp_debug("peer wants us to reset our connection");
			connection->state = netman_tcp_connection_state_closed;

			// release the connection's life reference
			netman_tcp_connection_release_internal(connection);

			events |= netman_tcp_connection_event_closed | netman_tcp_connection_event_reset;

			goto out;
		}

		if ((header->control_bits & netman_tcp_control_bit_syn) != 0) {
			// bad packet; we shouldn't get SYN in this state
			// let's tell our peer to reset and let's reset ourselves
			netman_tcp_debug("bad SYN packet in middle of connection");
			netman_tcp_send_reset_detect_type(connection->key->local_address, connection->key->local_port, connection->key->peer_address, connection->key->peer_port, header, netman_ipv4_packet_length(ip_packet));

			connection->state = netman_tcp_connection_state_closed;

			// release the connection's life reference
			netman_tcp_connection_release_internal(connection);

			events |= netman_tcp_connection_event_closed | netman_tcp_connection_event_reset;

			goto out;
		}

		if ((header->control_bits & netman_tcp_control_bit_ack) == 0) {
			// bad packet; just drop it
			netman_tcp_debug("packet with no ACK set");
			goto out;
		}

		if (ack != connection->tx_sequence_number + 1) {
			// bad ACK
			// let's tell our peer to reset and let's reset ourselves
			netman_tcp_debug("bad ACK during SYN_RECEIVED; resetting connection");
			netman_tcp_send_reset(connection->key->local_address, connection->key->local_port, connection->key->peer_address, connection->key->peer_port, ack, false);

			connection->state = netman_tcp_connection_state_closed;

			// release the connection's life reference
			netman_tcp_connection_release_internal(connection);

			events |= netman_tcp_connection_event_closed | netman_tcp_connection_event_reset;

			goto out;
		}

		connection->state = netman_tcp_connection_state_established;
		connection->tx_sequence_number = ferro_byteswap_big_to_native_u32(header->acknowledgement_number);
		connection->tx_max_sequence_number = connection->tx_sequence_number;

		if ((header->control_bits & netman_tcp_control_bit_fin) != 0) {
			// peer wants to close their end of the connection already
			netman_tcp_debug("peer closing their end of connection immediately");
			connection->state = netman_tcp_connection_state_close_wait;
			connection->rx_sequence_number = seq + 1;
			connection->pending_ack_send = true;

			events |= netman_tcp_connection_event_close_receive;
		}

		netman_tcp_connection_try_send(connection);

		events |= netman_tcp_connection_event_connected;
	} else if (
		connection->state == netman_tcp_connection_state_established ||
		connection->state == netman_tcp_connection_state_fin_wait_1  ||
		connection->state == netman_tcp_connection_state_fin_wait_2  ||
		connection->state == netman_tcp_connection_state_close_wait
	) {
		size_t data_offset = (size_t)(header->data_offset >> 4) * 4;
		size_t data_length = netman_ipv4_packet_length(ip_packet) - data_offset;
		uint32_t seq = ferro_byteswap_big_to_native_u32(header->sequence_number);
		uint32_t last_seq = seq + (data_length > 0 ? data_length - 1 : 0);
		uint32_t ack = ferro_byteswap_big_to_native_u32(header->acknowledgement_number);
		bool is_too_much = ack > connection->tx_max_sequence_number;

		if (
			(seq < connection->rx_sequence_number && last_seq < connection->rx_sequence_number) ||
			(seq > connection->rx_sequence_number)
			// TODO: this is actually non-spec-compliant; we're supposed to accept any range within the window we report
			//       (even unordered packets)
		) {
			if ((header->control_bits & netman_tcp_control_bit_rst) != 0) {
				// just ignore it
				netman_tcp_debug("ignoring out-of-order packet with RST bit set");
			} else {
				// we need to acknowledge the sequence number we were actually expecting
				netman_tcp_debugf("got seq %u (with last seq %u) while expecting seq %u", seq, last_seq, connection->rx_sequence_number);
				connection->pending_ack_send = true;
				netman_tcp_connection_try_send(connection);
			}

			goto out;
		}

		if ((header->control_bits & netman_tcp_control_bit_rst) != 0) {
			netman_tcp_debug("peer wants us to reset our connection");
			connection->state = netman_tcp_connection_state_closed;

			// release the connection's life reference
			netman_tcp_connection_release_internal(connection);

			events |= netman_tcp_connection_event_closed | netman_tcp_connection_event_reset;

			goto out;
		}

		if ((header->control_bits & netman_tcp_control_bit_syn) != 0) {
			// bad packet; we shouldn't get SYN in this state
			// let's tell our peer to reset and let's reset ourselves
			netman_tcp_debug("bad SYN packet in middle of connection");
			netman_tcp_send_reset_detect_type(connection->key->local_address, connection->key->local_port, connection->key->peer_address, connection->key->peer_port, header, netman_ipv4_packet_length(ip_packet));

			connection->state = netman_tcp_connection_state_closed;

			// release the connection's life reference
			netman_tcp_connection_release_internal(connection);

			events |= netman_tcp_connection_event_closed | netman_tcp_connection_event_reset;

			goto out;
		}

		if ((header->control_bits & netman_tcp_control_bit_ack) == 0) {
			// bad packet; just drop it
			netman_tcp_debug("ignoring packet with no ACK");
			goto out;
		}

		// the sequence number just beyond the max is the one for the FIN
		if (
			connection->state == netman_tcp_connection_state_fin_wait_1 && ack == connection->tx_max_sequence_number + 1
		) {
			is_too_much = false;
		}

		if (ack < connection->tx_sequence_number) {
			// duplicate ACK; just ignore it
			netman_tcp_debug("duplicate ACK packet");
			goto out;
		}

		if (is_too_much) {
			// we need to tell our peer where we actually are
			netman_tcp_debugf("ACK'd too much; ack=%u, actually at %u (max of %u)", ack, connection->tx_sequence_number, connection->tx_max_sequence_number);
			connection->pending_ack_send = true;
			netman_tcp_connection_try_send(connection);
			goto out;
		}

		if (connection->state == netman_tcp_connection_state_fin_wait_1 && ack == connection->tx_max_sequence_number + 1) {
			connection->tx_sequence_number = ack;
			connection->state = netman_tcp_connection_state_fin_wait_2;

			events |= netman_tcp_connection_event_close_send;
		} else if (ack > connection->tx_sequence_number) {
			sys_mutex_lock(&connection->tx_mutex);

			connection->tx_head = (connection->tx_head + (ack - connection->tx_sequence_number)) % connection->tx_ring_size;
			connection->tx_sequence_number = ack;
			connection->tx_ring_full = false;

			sys_mutex_unlock(&connection->tx_mutex);

			events |= netman_tcp_connection_event_data_sent;
		}

		if (data_length > 0) {
			data_offset += connection->rx_sequence_number - seq;
			data_length -= connection->rx_sequence_number - seq;

			sys_mutex_lock(&connection->rx_mutex);

			if (!connection->rx_ring_full) {
				size_t space = (connection->rx_tail >= connection->rx_head) ? (connection->rx_ring_size - (connection->rx_tail - connection->rx_head)) : (connection->rx_head - connection->rx_tail);
				size_t end_space = (connection->rx_tail >= connection->rx_head) ? (connection->rx_ring_size - connection->rx_tail) : 0;
				size_t rx_size = (end_space < data_length) ? end_space : data_length;

				simple_memcpy(&((char*)connection->rx_ring)[connection->rx_tail], (char*)header + data_offset, rx_size);
				connection->rx_tail = (connection->rx_tail + rx_size) % connection->rx_ring_size;
				data_offset += rx_size;
				data_length -= rx_size;
				connection->rx_sequence_number += rx_size;
				space -= rx_size;

				rx_size = (space < data_length) ? space : data_length;

				simple_memcpy(&((char*)connection->rx_ring)[connection->rx_tail], (char*)header + data_offset, rx_size);
				connection->rx_tail = (connection->rx_tail + rx_size) % connection->rx_ring_size;
				data_offset += rx_size;
				data_length -= rx_size;
				connection->rx_sequence_number += rx_size;
				space -= rx_size;

				if (connection->rx_tail == connection->rx_head) {
					connection->rx_ring_full = true;
				}
			}

			sys_mutex_unlock(&connection->rx_mutex);

			connection->pending_ack_send = true;

			events |= netman_tcp_connection_event_data_received;
		}

		if (seq == connection->rx_sequence_number && (header->control_bits & netman_tcp_control_bit_fin) != 0) {
			if (connection->state == netman_tcp_connection_state_established) {
				connection->state = netman_tcp_connection_state_close_wait;
				connection->rx_sequence_number = seq + 1;
				connection->pending_ack_send = true;

				events |= netman_tcp_connection_event_close_receive;
			} else if (connection->state == netman_tcp_connection_state_fin_wait_2) {
				connection->state = netman_tcp_connection_state_time_wait;
				connection->rx_sequence_number = seq + 1;
				connection->pending_ack_send = true;

				sys_abort_status_log(netman_tcp_connection_retain_internal(connection));

				if (eve_loop_schedule(eve_loop_get_main(), (void*)netman_tcp_connection_timed_wait_expire, connection, NETMAN_TCP_DEFAULT_TIME_WAIT_MS * 1000000, sys_timeout_type_relative_ns_monotonic, NULL) != ferr_ok) {
					// failed to schedule a timed expiry; let's just clean it up now
					netman_tcp_connection_timed_wait_expire(connection);
				}

				events |= netman_tcp_connection_event_closed;
			} else {
				netman_tcp_debug("bad state?");
			}
		}

		netman_tcp_connection_try_send(connection);
	} else if (connection->state == netman_tcp_connection_state_last_ack) {
		uint32_t seq = ferro_byteswap_big_to_native_u32(header->sequence_number);
		uint32_t ack = ferro_byteswap_big_to_native_u32(header->acknowledgement_number);

		if (seq != connection->rx_sequence_number) {
			// bad packet

			if ((header->control_bits & netman_tcp_control_bit_rst) != 0) {
				// ignore the packet
				netman_tcp_debug("last ACK: ignoring out-of-order packet with RST bit set");
			} else {
				// let's tell them what we were actually expecting
				netman_tcp_debugf("last ACK: received out-of-order packet (seq=%u, expected=%u); informing with ACK", seq, connection->rx_sequence_number);
				connection->pending_ack_send = true;

				netman_tcp_connection_try_send(connection);
			}

			goto out;
		}

		if ((header->control_bits & netman_tcp_control_bit_rst) != 0) {
			netman_tcp_debug("last ACK: peer wants us to reset our connection");
			connection->state = netman_tcp_connection_state_closed;

			// release the connection's life reference
			netman_tcp_connection_release_internal(connection);

			events |= netman_tcp_connection_event_closed | netman_tcp_connection_event_reset;

			goto out;
		}

		if ((header->control_bits & netman_tcp_control_bit_syn) != 0) {
			// bad packet; we shouldn't get SYN in this state
			// let's tell our peer to reset and let's reset ourselves
			netman_tcp_debug("last ACK: bad SYN packet in middle of connection");
			netman_tcp_send_reset_detect_type(connection->key->local_address, connection->key->local_port, connection->key->peer_address, connection->key->peer_port, header, netman_ipv4_packet_length(ip_packet));

			connection->state = netman_tcp_connection_state_closed;

			// release the connection's life reference
			netman_tcp_connection_release_internal(connection);

			events |= netman_tcp_connection_event_closed | netman_tcp_connection_event_reset;

			goto out;
		}

		if ((header->control_bits & netman_tcp_control_bit_ack) == 0) {
			// just ignore it
			netman_tcp_debug("last ACK: ignoring packet with no ACK bit");
			goto out;
		}

		if (ack != connection->tx_sequence_number + 1) {
			netman_tcp_debugf("last ACK: bad packet; ACK is %u, expected %u", ack, connection->tx_sequence_number + 1);
			goto out;
		}

		connection->state = netman_tcp_connection_state_closed;

		// release the connection's life reference
		// (held only while the connection is alive/open)
		netman_tcp_connection_release_internal(connection);

		events |= netman_tcp_connection_event_closed;
	} else if (connection->state == netman_tcp_connection_state_closed || connection->state == netman_tcp_connection_state_time_wait) {
		// discard the packet and send back a reset packet
		netman_tcp_send_reset_detect_type(connection->key->local_address, connection->key->local_port, connection->key->peer_address, connection->key->peer_port, header, netman_ipv4_packet_length(ip_packet));
	}

out:
	if (events != 0) {
		if (connection->handler) {
			connection->handler(connection->handler_context, (void*)connection, events);
		} else {
			netman_tcp_debugf("wanted to trigger handler on connection %p with events=0x%02x, but no handler was installed", connection, events);
		}
	}
	netman_tcp_connection_release_internal(connection);
	netman_ipv4_packet_destroy(ip_packet);
};

static void netman_tcp_listener_handle_packet(netman_tcp_listener_object_t* listener, netman_ipv4_packet_t* ip_packet, const netman_tcp_key_t* lookup_key) {
	netman_tcp_header_t* header = NULL;

	if (netman_ipv4_packet_map(ip_packet, (void*)&header, NULL) != ferr_ok) {
		return;
	}

	sys_mutex_lock(&listener->pending_mutex);

	if (listener->pending_ring_full) {
		// send back a reset packet
		sys_mutex_unlock(&listener->pending_mutex);

		netman_tcp_debug("not enough space for incoming connection; sending back a reset");

		netman_tcp_send_reset_detect_type(lookup_key->local_address, lookup_key->local_port, lookup_key->peer_address, lookup_key->peer_port, header, netman_ipv4_packet_length(ip_packet));

		netman_ipv4_packet_destroy(ip_packet);
		return;
	}

	listener->pending_ring[listener->pending_tail] = ip_packet;
	listener->pending_tail = (listener->pending_tail + 1) % listener->pending_ring_size;

	if (listener->pending_head == listener->pending_tail) {
		listener->pending_ring_full = true;
	}

	sys_mutex_unlock(&listener->pending_mutex);

	listener->listener(listener->listener_context, (void*)listener);

	netman_release((void*)listener);
};

ferr_t netman_tcp_handle_packet(netman_ipv4_packet_t* ip_packet) {
	ferr_t status = ferr_ok;
	netman_tcp_header_t* header = NULL;
	size_t length = 0;
	netman_tcp_connection_object_t* connection = NULL;
	netman_tcp_key_t lookup_key;

	if (netman_ipv4_packet_length(ip_packet) < sizeof(*header)) {
		status = ferr_too_small;
		goto out;
	}

	status = netman_ipv4_packet_map(ip_packet, (void*)&header, &length);
	if (status != ferr_ok) {
		goto out;
	}

	simple_memset(&lookup_key, 0, sizeof(lookup_key));
	lookup_key.peer_address = netman_ipv4_packet_get_source_address(ip_packet);
	lookup_key.local_address = netman_ipv4_packet_get_destination_address(ip_packet);
	lookup_key.peer_port = ferro_byteswap_big_to_native_u16(header->source_port);
	lookup_key.local_port = ferro_byteswap_big_to_native_u16(header->destination_port);

	netman_tcp_debugf(
		"looking for existing connection from local=%u.%u.%u.%u:%u to peer=%u.%u.%u.%u:%u",
		NETMAN_IPV4_OCTET_A(lookup_key.local_address),
		NETMAN_IPV4_OCTET_B(lookup_key.local_address),
		NETMAN_IPV4_OCTET_C(lookup_key.local_address),
		NETMAN_IPV4_OCTET_D(lookup_key.local_address),
		lookup_key.local_port,
		NETMAN_IPV4_OCTET_A(lookup_key.peer_address),
		NETMAN_IPV4_OCTET_B(lookup_key.peer_address),
		NETMAN_IPV4_OCTET_C(lookup_key.peer_address),
		NETMAN_IPV4_OCTET_D(lookup_key.peer_address),
		lookup_key.peer_port
	);

	sys_mutex_lock(&connection_table_mutex);

	status = simple_ghmap_lookup(&connection_table, &lookup_key, sizeof(lookup_key), false, 0, NULL, (void*)&connection, NULL);
	if (status == ferr_ok) {
		sys_abort_status_log(netman_tcp_connection_retain_internal(connection));
	}

	sys_mutex_unlock(&connection_table_mutex);

	if (status == ferr_ok) {
		netman_tcp_connection_handle_packet(connection, ip_packet);
		ip_packet = NULL;
	} else if (status == ferr_no_such_resource && lookup_key.local_port < NETMAN_TCP_DYNAMIC_PORT_START) {
		// this might be a client trying to connect
		netman_tcp_listener_object_t* listener = NULL;

		if (header->control_bits != netman_tcp_control_bit_syn) {
			// bad packet; new incoming connection must have only the SYN bit set

			if ((header->control_bits & netman_tcp_control_bit_ack) != 0) {
				// if it has an ACK, send back a reset packet
				netman_tcp_send_reset(lookup_key.local_address, lookup_key.local_port, lookup_key.peer_address, lookup_key.peer_port, ferro_byteswap_big_to_native_u32(header->acknowledgement_number), false);
			}

			goto out;
		}

		sys_mutex_lock(&port_table_mutex);

		status = simple_ghmap_lookup_h(&port_table, lookup_key.local_port, false, 0, NULL, (void*)&listener, NULL);
		if (status == ferr_ok) {
			sys_abort_status_log(netman_retain((void*)listener));
		}

		sys_mutex_unlock(&port_table_mutex);

		if (status == ferr_ok) {
			netman_tcp_listener_handle_packet(listener, ip_packet, &lookup_key);
			ip_packet = NULL;
		} else {
			// someone sent us a packet for a port that we weren't listening on
			// send back a reset packet
			netman_tcp_send_reset_detect_type(lookup_key.local_address, lookup_key.local_port, lookup_key.peer_address, lookup_key.peer_port, header, netman_ipv4_packet_length(ip_packet));
		}
	} else {
		// someone sent us a packet for a dynamic port but we didn't have any connection allocated on that port
		netman_tcp_send_reset_detect_type(lookup_key.local_address, lookup_key.local_port, lookup_key.peer_address, lookup_key.peer_port, header, netman_ipv4_packet_length(ip_packet));
	}

out:
	if (status == ferr_ok) {
		if (ip_packet) {
			netman_ipv4_packet_destroy(ip_packet);
		}
	} else {
		netman_tcp_debugf("returning status=%d (%s)", status, ferr_name(status));
	}
	return status;
};

static ferr_t netman_tcp_connection_allocate(uint32_t peer_address, uint16_t peer_port, uint32_t local_address, uint16_t local_port, netman_ipv4_packet_t* pending_packet, void* rx_ring, size_t rx_ring_size, void* tx_ring, size_t tx_ring_size, bool lock_held, netman_tcp_connection_object_t** out_connection) {
	ferr_t status = ferr_ok;
	netman_tcp_connection_object_t* connection = NULL;
	bool created = false;
	netman_tcp_key_t lookup_key;

	// TODO: object-ify this.

	simple_memset(&lookup_key, 0, sizeof(lookup_key));
	lookup_key.local_address = local_address;
	lookup_key.local_port = local_port;
	lookup_key.peer_address = peer_address;
	lookup_key.peer_port = peer_port;

	if (!rx_ring) {
		if (rx_ring_size == 0) {
			rx_ring_size = NETMAN_TCP_DEFAULT_RX_RING_SIZE;
		}
		status = sys_mempool_allocate(rx_ring_size, NULL, &rx_ring);
		if (status != ferr_ok) {
			goto out;
		}
	}

	if (!tx_ring) {
		if (tx_ring_size == 0) {
			tx_ring_size = NETMAN_TCP_DEFAULT_TX_RING_SIZE;
		}
		status = sys_mempool_allocate(tx_ring_size, NULL, &tx_ring);
		if (status != ferr_ok) {
			goto out;
		}
	}

	if (!lock_held) {
		sys_mutex_lock(&connection_table_mutex);
	}

	status = simple_ghmap_lookup(&connection_table, &lookup_key, sizeof(lookup_key), true, sizeof(*connection), &created, (void*)&connection, NULL);
	if (status != ferr_ok) {
		if (!lock_held) {
			sys_mutex_unlock(&connection_table_mutex);
		}
		goto out;
	}

	if (!created) {
		if (!lock_held) {
			sys_mutex_unlock(&connection_table_mutex);
		}
		status = ferr_resource_unavailable;
		goto out;
	}

	simple_memset(connection, 0, sizeof(*connection));

	status = sys_object_init(&connection->object, &tcp_connection_object_class);
	if (status != ferr_ok) {
		if (!lock_held) {
			sys_mutex_unlock(&connection_table_mutex);
		}
		status = ferr_temporary_outage;
		goto out;
	}

	connection->state = netman_tcp_connection_state_closed;
	// two internal references:
	// 1. the external (user) reference.
	// 2. the lifetime reference that lasts for as long as the connection is alive.
	connection->internal_refcount = 2;

	// this CANNOT fail
	sys_abort_status_log(simple_ghmap_lookup_stored_key(&connection_table, &lookup_key, sizeof(lookup_key), (void*)&connection->key, NULL));

	sys_mutex_init(&connection->rx_mutex);
	sys_mutex_init(&connection->tx_mutex);
	sys_mutex_init(&connection->retransmit_mutex);

	connection->retransmit_work_id = eve_loop_work_id_invalid;

	connection->rx_ring = rx_ring;
	rx_ring = NULL;

	connection->rx_ring_size = rx_ring_size;

	connection->tx_ring = tx_ring;
	tx_ring = NULL;

	connection->tx_ring_size = tx_ring_size;

	connection->pending_packet = pending_packet;

	if (!lock_held) {
		sys_mutex_unlock(&connection_table_mutex);
	}

out:
	if (status == ferr_ok) {
		*out_connection = connection;
	} else {
		if (rx_ring) {
			NETMAN_WUR_IGNORE(sys_mempool_free(rx_ring));
		}

		if (tx_ring) {
			NETMAN_WUR_IGNORE(sys_mempool_free(tx_ring));
		}
	}
	return status;
};

//
// public API
//

ferr_t netman_tcp_listen(netman_tcp_port_number_t port_number, netman_tcp_listener_f listener_handler, void* context, netman_tcp_listener_t** out_listener) {
	ferr_t status = ferr_ok;
	netman_tcp_listener_object_t* listener = NULL;
	bool created = false;
	netman_ipv4_packet_t** pending_ring = NULL;

	if (port_number >= NETMAN_TCP_DYNAMIC_PORT_START) {
		status = ferr_invalid_argument;
		goto out;
	}

	status = sys_mempool_allocate(sizeof(*pending_ring) * NETMAN_TCP_DEFAULT_PENDING_RING_SIZE, NULL, (void*)&pending_ring);
	if (status != ferr_ok) {
		goto out;
	}

	sys_mutex_lock(&port_table_mutex);

	status = simple_ghmap_lookup_h(&port_table, port_number, true, sizeof(*listener), &created, (void*)&listener, NULL);
	if (status != ferr_ok) {
		sys_mutex_unlock(&port_table_mutex);
		goto out;
	}

	if (!created) {
		sys_mutex_unlock(&port_table_mutex);
		status = ferr_resource_unavailable;
		goto out;
	}

	simple_memset(listener, 0, sizeof(*listener));

	status = sys_object_init(&listener->object, &tcp_listener_object_class);
	if (status != ferr_ok) {
		sys_mutex_unlock(&port_table_mutex);
		status = ferr_temporary_outage;
		goto out;
	}

	listener->port_number = port_number;
	listener->listener = listener_handler;
	listener->listener_context = context;

	sys_mutex_init(&listener->pending_mutex);

	listener->pending_ring = pending_ring;
	pending_ring = NULL;

	listener->pending_ring_size = NETMAN_TCP_DEFAULT_PENDING_RING_SIZE;

	sys_mutex_unlock(&port_table_mutex);

out:
	if (status == ferr_ok) {
		*out_listener = (void*)listener;
	} else {
		if (pending_ring) {
			NETMAN_WUR_IGNORE(sys_mempool_free(pending_ring));
		}
	}
	return status;
};

size_t netman_tcp_listener_accept(netman_tcp_listener_t* obj, netman_tcp_connection_t** out_connections, size_t array_space) {
	size_t accepted = 0;
	netman_tcp_listener_object_t* listener = (void*)obj;

	if (array_space == 0) {
		goto out;
	}

	sys_mutex_lock(&listener->pending_mutex);

	if ((!listener->pending_ring_full && listener->pending_head == listener->pending_tail) || listener->pending_ring_size == 0) {
		// empty ring
		sys_mutex_unlock(&listener->pending_mutex);
		goto out;
	}

	for (bool is_first = true; (is_first || listener->pending_head != listener->pending_tail) && accepted < array_space; listener->pending_head = (listener->pending_head + 1) % listener->pending_ring_size) {
		netman_ipv4_packet_t* packet = listener->pending_ring[listener->pending_head];
		netman_tcp_header_t* header = NULL;
		netman_tcp_connection_object_t* connection = NULL;

		if (is_first) {
			is_first = false;
		}

		if (netman_ipv4_packet_map(packet, (void*)&header, NULL) != ferr_ok) {
			break;
		}

		if (netman_tcp_connection_allocate(netman_ipv4_packet_get_source_address(packet), ferro_byteswap_big_to_native_u16(header->source_port), netman_ipv4_packet_get_destination_address(packet), ferro_byteswap_big_to_native_u16(header->destination_port), packet, NULL, 0, NULL, 0, false, &connection) != ferr_ok) {
			break;
		}

		// the connection now owns the packet

		out_connections[accepted] = (void*)connection;
		++accepted;

		// and the caller now owns the connection
	}

	sys_mutex_unlock(&listener->pending_mutex);

out:
	return accepted;
};

ferr_t netman_tcp_connect(uint32_t address, const uint8_t* mac, netman_tcp_port_number_t port, netman_tcp_connection_handler_f handler, void* context, netman_tcp_connection_t** out_connection) {
	ferr_t status = ferr_ok;
	netman_tcp_connection_object_t* connection = NULL;
	void* rx_ring = NULL;
	void* tx_ring = NULL;
	uint16_t local_port = 0;

	status = sys_mempool_allocate(NETMAN_TCP_DEFAULT_RX_RING_SIZE, NULL, &rx_ring);
	if (status != ferr_ok) {
		goto out;
	}

	status = sys_mempool_allocate(NETMAN_TCP_DEFAULT_TX_RING_SIZE, NULL, &tx_ring);
	if (status != ferr_ok) {
		goto out;
	}

	status = ferr_resource_unavailable;

	sys_mutex_lock(&connection_table_mutex);

	bool is_first = true;
	for (uint16_t i = next_dynamic_port; is_first || i != next_dynamic_port; (i = (i + 1) % NETMAN_TCP_DYNAMIC_PORT_COUNT)) {
		if (is_first) {
			is_first = false;
		}

		status = netman_tcp_connection_allocate(address, port, NETMAN_IPV4_STATIC_ADDRESS, NETMAN_TCP_DYNAMIC_PORT_START + i, NULL, rx_ring, NETMAN_TCP_DEFAULT_RX_RING_SIZE, tx_ring, NETMAN_TCP_DEFAULT_TX_RING_SIZE, true, &connection);
		if (status != ferr_ok) {
			continue;
		}

		local_port = NETMAN_TCP_DYNAMIC_PORT_START + i;

		break;
	}

	if (status != ferr_ok) {
		sys_mutex_unlock(&connection_table_mutex);
		goto out;
	}

	next_dynamic_port = ((local_port - NETMAN_TCP_DYNAMIC_PORT_START) + 1) % NETMAN_TCP_DYNAMIC_PORT_COUNT;

	sys_mutex_unlock(&connection_table_mutex);

out:
	if (status == ferr_ok) {
		*out_connection = (void*)connection;

		connection->state = netman_tcp_connection_state_syn_sent;
		connection->handler_context = context;
		connection->handler = handler;

		netman_tcp_connection_try_send(connection);
	} else {
		if (rx_ring) {
			NETMAN_WUR_IGNORE(sys_mempool_free(rx_ring));
		}

		if (tx_ring) {
			NETMAN_WUR_IGNORE(sys_mempool_free(tx_ring));
		}
	}
	return status;
};

ferr_t netman_tcp_connection_receive(netman_tcp_connection_t* obj, void* buffer, size_t buffer_size, size_t* out_received) {
	netman_tcp_connection_object_t* connection = (void*)obj;
	ferr_t status = ferr_ok;
	size_t received = 0;
	size_t space = 0;
	size_t end_space = 0;
	size_t rx_size = 0;

	if (buffer_size == 0) {
		goto out;
	}

	sys_mutex_lock(&connection->rx_mutex);

	if ((!connection->rx_ring_full && connection->rx_head == connection->rx_tail) || connection->rx_ring_size == 0) {
		// empty ring
		sys_mutex_unlock(&connection->rx_mutex);
		goto out;
	}

	space = (connection->rx_tail > connection->rx_head) ? (connection->rx_tail - connection->rx_head) : (connection->rx_ring_size - (connection->rx_head - connection->rx_tail));
	end_space = (connection->rx_tail < connection->rx_head) ? (connection->rx_ring_size - connection->rx_head) : 0;
	rx_size = (end_space < buffer_size) ? end_space : buffer_size;

	simple_memcpy((char*)buffer + received, &((char*)connection->rx_ring)[connection->rx_head], rx_size);
	connection->rx_head = (connection->rx_head + rx_size) % connection->rx_ring_size;
	received += rx_size;
	buffer_size -= rx_size;
	space -= rx_size;

	rx_size = (space < buffer_size) ? space : buffer_size;

	simple_memcpy((char*)buffer + received, &((char*)connection->rx_ring)[connection->rx_head], rx_size);
	connection->rx_head = (connection->rx_head + rx_size) % connection->rx_ring_size;
	received += rx_size;
	buffer_size -= rx_size;
	space -= rx_size;

	connection->rx_ring_full = false;

	sys_mutex_unlock(&connection->rx_mutex);

out:
	if (out_received) {
		*out_received = received;
	}
	return status;
};

ferr_t netman_tcp_connection_send(netman_tcp_connection_t* obj, const void* buffer, size_t buffer_length, size_t* out_sent) {
	netman_tcp_connection_object_t* connection = (void*)obj;
	ferr_t status = ferr_ok;
	size_t sent = 0;
	size_t space = 0;
	size_t end_space = 0;
	size_t tx_size = 0;

	if (buffer_length == 0) {
		goto out;
	}

	sys_mutex_lock(&connection->tx_mutex);

	if (connection->tx_ring_full) {
		sys_mutex_unlock(&connection->tx_mutex);
		goto out;
	}

	space = (connection->tx_tail >= connection->tx_head) ? (connection->tx_ring_size - (connection->tx_tail - connection->tx_head)) : (connection->tx_head - connection->tx_tail);
	end_space = (connection->tx_tail > connection->tx_head) ? (connection->tx_ring_size - connection->tx_tail) : 0;
	tx_size = (end_space < buffer_length) ? end_space : buffer_length;

	simple_memcpy(&((char*)connection->tx_ring)[connection->tx_tail], (char*)buffer + sent, tx_size);
	connection->tx_tail = (connection->tx_tail + tx_size) % connection->tx_ring_size;
	sent += tx_size;
	buffer_length -= tx_size;
	connection->tx_max_sequence_number += tx_size;
	space -= tx_size;

	tx_size = (space < buffer_length) ? space : buffer_length;

	simple_memcpy(&((char*)connection->tx_ring)[connection->tx_tail], (char*)buffer + sent, tx_size);
	connection->tx_tail = (connection->tx_tail + tx_size) % connection->tx_ring_size;
	sent += tx_size;
	buffer_length -= tx_size;
	connection->tx_max_sequence_number += tx_size;
	space -= tx_size;

	if (connection->tx_tail == connection->tx_head) {
		connection->tx_ring_full = true;
	}

	sys_mutex_unlock(&connection->tx_mutex);

out:
	if (out_sent) {
		*out_sent = sent;
	}
	if (sent > 0) {
		netman_tcp_connection_try_send(connection);
	}
	return status;
};

void netman_tcp_connection_close(netman_tcp_connection_t* obj) {
	netman_tcp_connection_object_t* connection = (void*)obj;
	switch (connection->state) {
		case netman_tcp_connection_state_closed:
		case netman_tcp_connection_state_fin_wait_1:
		case netman_tcp_connection_state_fin_wait_2:
		case netman_tcp_connection_state_closing:
		case netman_tcp_connection_state_last_ack:
		case netman_tcp_connection_state_time_wait:
			return;

		case netman_tcp_connection_state_close_wait:
			connection->state = netman_tcp_connection_state_last_ack;
			netman_tcp_connection_try_send(connection);
			break;

		default:
			connection->state = netman_tcp_connection_state_fin_wait_1;
			netman_tcp_connection_try_send(connection);
			break;
	}
};

void netman_tcp_connection_set_handler(netman_tcp_connection_t* obj, netman_tcp_connection_handler_f handler, void* context) {
	netman_tcp_connection_object_t* connection = (void*)obj;
	connection->handler = handler;
	connection->handler_context = context;
};

ferr_t netman_tcp_connection_resume(netman_tcp_connection_t* obj) {
	netman_tcp_connection_object_t* connection = (void*)obj;
	if (connection->pending_packet) {
		connection->state = netman_tcp_connection_state_closed_init;

		sys_abort_status_log(netman_tcp_connection_retain_internal(connection));
		netman_tcp_connection_handle_packet(connection, connection->pending_packet);
		connection->pending_packet = NULL;
		return ferr_ok;
	} else {
		return ferr_already_in_progress;
	}
};
