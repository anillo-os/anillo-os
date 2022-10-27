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

#include <netman/icmp.private.h>
#include <netman/ip.h>
#include <libsimple/libsimple.h>
#include <netman/device.h>

// for netman_ipv4_compute_checksum() only
#include <netman/ip.private.h>

#ifndef NETMAN_ICMP_DEBUG
	#define NETMAN_ICMP_DEBUG 0
#endif

#if NETMAN_ICMP_DEBUG
	#define netman_icmp_debug_f(...) sys_console_log_f(__VA_ARGS__)
#else
	#define netman_icmp_debug_f(...)
#endif

ferr_t netman_icmp_handle_packet(netman_ipv4_packet_t* ip_packet) {
	ferr_t status = ferr_ok;
	netman_icmp_header_t* header = NULL;
	size_t length = 0;

	status = netman_ipv4_packet_map(ip_packet, (void*)&header, &length);
	if (status != ferr_ok) {
		netman_icmp_debug_f("ICMP: failed to map IPv4 packet\n");
		goto out;
	}

	if (length < sizeof(netman_icmp_header_t)) {
		netman_icmp_debug_f("ICMP: packet is too small for ICMP header\n");
		status = ferr_too_small;
		goto out;
	}

	if (header->type == netman_icmp_type_echo_request) {
		netman_icmp_echo_header_t* echo_header = (void*)header;
		netman_ipv4_packet_t* reply = NULL;
		ferr_t status2 = ferr_ok;
		netman_icmp_echo_header_t* reply_header = NULL;
		uint8_t dest_mac[6];

		if (length < sizeof(netman_icmp_echo_header_t)) {
			netman_icmp_debug_f("ICMP: packet is too small for ICMP echo request header\n");
			status = ferr_too_small;
			goto out;
		}

		status2 = netman_ipv4_packet_get_source_mac(ip_packet, dest_mac);
		if (status2 != ferr_ok) {
			netman_icmp_debug_f("ICMP: failed to get source MAC\n");
			goto echo_out;
		}

		status2 = netman_ipv4_packet_create(&reply);
		if (status2 != ferr_ok) {
			netman_icmp_debug_f("ICMP: failed to create reply packet\n");
			goto echo_out;
		}

		status2 = netman_ipv4_packet_extend(reply, length, false, NULL);
		if (status2 != ferr_ok) {
			netman_icmp_debug_f("ICMP: failed to extend reply packet\n");
			goto echo_out;
		}

		status2 = netman_ipv4_packet_map(reply, (void*)&reply_header, NULL);
		if (status2 != ferr_ok) {
			netman_icmp_debug_f("ICMP: failed to map reply packet\n");
			goto echo_out;
		}

		status2 = netman_ipv4_packet_set_protocol(reply, netman_ipv4_protocol_type_icmp);
		if (status2 != ferr_ok) {
			netman_icmp_debug_f("ICMP: failed to set protocol for reply packet\n");
			goto echo_out;
		}

		status2 = netman_ipv4_packet_set_destination_mac(reply, dest_mac);
		if (status2 != ferr_ok) {
			netman_icmp_debug_f("ICMP: failed to set destination MAC for reply packet\n");
			goto echo_out;
		}

		status2 = netman_ipv4_packet_set_destination_address(reply, netman_ipv4_packet_get_source_address(ip_packet));
		if (status2 != ferr_ok) {
			netman_icmp_debug_f("ICMP: failed to set destination address for reply packet\n");
			goto echo_out;
		}

		reply_header->header.type = netman_icmp_type_echo_reply;
		reply_header->header.code = 0;
		reply_header->header.checksum = 0;
		reply_header->identifier = echo_header->identifier;
		reply_header->sequence_number = echo_header->sequence_number;

		simple_memcpy(reply_header + 1, echo_header + 1, length - sizeof(*echo_header));

		reply_header->header.checksum = netman_ipv4_compute_checksum(reply_header, length);

		status2 = netman_ipv4_packet_transmit(reply, netman_device_any());
		if (status2 != ferr_ok) {
			netman_icmp_debug_f("ICMP: failed to transmit reply packet\n");
		}

echo_out:;
		if (status2 != ferr_ok) {
			if (reply) {
				netman_ipv4_packet_destroy(reply);
			}
		}
	} else {
		status = ferr_unknown;
	}

out:
	if (status == ferr_ok) {
		netman_ipv4_packet_destroy(ip_packet);
	}
	return status;
};
