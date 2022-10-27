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

#include <netman/ether.private.h>
#include <ferro/byteswap.h>
#include <libsimple/libsimple.h>
#include <netman/arp.h>
#include <netman/ip.h>
#include <netman/packet.h>
#include <netman/device.private.h>

#ifndef NETMAN_ETHER_LOG
	#define NETMAN_ETHER_LOG 1
#endif

static ferr_t netman_ether_packet_receive(void* context, netman_packet_t* packet) {
	const netman_ether_packet_t* data = NULL;
	size_t length = netman_packet_length(packet);
	ferr_t status = ferr_ok;

	if (length < 14) {
		// not our packet; we need at least 14 bytes for our header
		return ferr_unknown;
	}

	status = netman_packet_map(packet, (void*)&data, NULL);
	if (status != ferr_ok) {
		return status;
	}

	uint16_t ethertype = ferro_byteswap_big_to_native_u16(data->ethertype);

#if NETMAN_ETHER_LOG
	sys_console_log_f(
		"Received packet: %zu bytes, source=%02x:%02x:%02x:%02x:%02x:%02x, dest=%02x:%02x:%02x:%02x:%02x:%02x, ethertype=%04x\n",
		netman_packet_length(packet),
		data->source[0],
		data->source[1],
		data->source[2],
		data->source[3],
		data->source[4],
		data->source[5],
		data->destination[0],
		data->destination[1],
		data->destination[2],
		data->destination[3],
		data->destination[4],
		data->destination[5],
		ethertype
	);
#endif

	if (ethertype == netman_ether_packet_type_arp) {
		status = netman_arp_handle_packet(packet, offsetof(netman_ether_packet_t, payload));
	} else if (ethertype == netman_ether_packet_type_ipv4) {
		status = netman_ipv4_handle_packet(packet, offsetof(netman_ether_packet_t, payload));
	} else {
		// we don't know how to handle this packet
		status = ferr_unknown;
	}

	return status;
};

void netman_ether_init(void) {
	sys_abort_status_log(netman_device_register_global_packet_receive_hook(netman_ether_packet_receive, NULL));
};

ferr_t netman_ether_packet_write_header(netman_packet_t* packet, const uint8_t* source_mac, const uint8_t* destination_mac, netman_ether_packet_type_t packet_type, size_t* out_payload_offset) {
	ferr_t status = ferr_ok;
	netman_ether_packet_t* ether_packet = NULL;

	status = netman_packet_map(packet, (void*)&ether_packet, NULL);
	if (status != ferr_ok) {
		goto out;
	}

	simple_memcpy(&ether_packet->source[0], source_mac, 6);
	simple_memcpy(&ether_packet->destination[0], destination_mac, 6);

	ether_packet->ethertype = ferro_byteswap_native_to_big_u16(packet_type);

out:
	if (status == ferr_ok) {
		if (out_payload_offset) {
			*out_payload_offset = offsetof(netman_ether_packet_t, payload);
		}
	}
	return status;
};

ferr_t netman_ether_packet_set_source_mac(netman_packet_t* packet, const uint8_t* source_mac) {
	ferr_t status = ferr_ok;
	netman_ether_packet_t* ether_packet = NULL;

	status = netman_packet_map(packet, (void*)&ether_packet, NULL);
	if (status != ferr_ok) {
		goto out;
	}

	simple_memcpy(&ether_packet->source[0], source_mac, 6);

out:
	return status;
};

ferr_t netman_ether_packet_set_destination_mac(netman_packet_t* packet, const uint8_t* destination_mac) {
	ferr_t status = ferr_ok;
	netman_ether_packet_t* ether_packet = NULL;

	status = netman_packet_map(packet, (void*)&ether_packet, NULL);
	if (status != ferr_ok) {
		goto out;
	}

	simple_memcpy(&ether_packet->destination[0], destination_mac, 6);

out:
	return status;
};

ferr_t netman_ether_packet_get_source_mac(netman_packet_t* packet, uint8_t* out_source_mac) {
	ferr_t status = ferr_ok;
	netman_ether_packet_t* ether_packet = NULL;

	status = netman_packet_map(packet, (void*)&ether_packet, NULL);
	if (status != ferr_ok) {
		goto out;
	}

	simple_memcpy(out_source_mac, &ether_packet->source[0], 6);

out:
	return status;
};

ferr_t netman_ether_packet_get_destination_mac(netman_packet_t* packet, uint8_t* out_destination_mac) {
	ferr_t status = ferr_ok;
	netman_ether_packet_t* ether_packet = NULL;

	status = netman_packet_map(packet, (void*)&ether_packet, NULL);
	if (status != ferr_ok) {
		goto out;
	}

	simple_memcpy(out_destination_mac, &ether_packet->destination[0], 6);

out:
	return status;
};
