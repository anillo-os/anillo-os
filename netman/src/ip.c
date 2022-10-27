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

#include <netman/ip.private.h>
#include <netman/packet.h>
#include <libsimple/libsimple.h>
#include <ferro/byteswap.h>
#include <netman/ether.h>
#include <netman/arp.h>
#include <netman/device.h>
#include <netman/icmp.h>
#include <netman/udp.h>
#include <netman/tcp.h>

#ifndef NETMAN_IP_LOG
	#define NETMAN_IP_LOG 0
#endif

static simple_ghmap_t reassembly_table;

void netman_ipv4_init(void) {
	sys_abort_status_log(simple_ghmap_init(&reassembly_table, 2, 0, simple_ghmap_allocate_sys_mempool, simple_ghmap_free_sys_mempool, simple_ghmap_hash_data, simple_ghmap_compares_equal_data, NULL, NULL, NULL, NULL));
	sys_abort_status_log(netman_arp_register_ipv4(NETMAN_IPV4_LOCAL_BROADCAST_ADDRESS, netman_ether_broadcast_address));
};

ferr_t netman_ipv4_reassembly_buffer_lookup(uint32_t source_address, uint32_t destination_address, uint16_t fragment_identifier, uint8_t protocol, netman_ipv4_reassembly_buffer_t** out_buffer) {
	ferr_t status = ferr_ok;
	netman_ipv4_reassembly_identifier_t identifier = {
		.source_address = source_address,
		.destination_address = destination_address,
		.fragment_identifier = fragment_identifier,
		.protocol = protocol,
	};
	bool created = false;
	netman_ipv4_reassembly_buffer_t* buffer = NULL;

	status = simple_ghmap_lookup(&reassembly_table, &identifier, sizeof(identifier), true, sizeof(netman_ipv4_reassembly_buffer_t), &created, (void*)&buffer, NULL);
	if (status != ferr_ok) {
		goto out;
	}

	if (created) {
		simple_memset(buffer, 0, sizeof(*buffer));

		sys_abort_status_log(simple_ghmap_lookup_stored_key(&reassembly_table, &identifier, sizeof(identifier), (void*)&buffer->identifier, NULL));
	}

out:
	if (status == ferr_ok) {
		*out_buffer = buffer;
	}
	return status;
};

void netman_ipv4_reassembly_buffer_clear(uint32_t source_address, uint32_t destination_address, uint16_t fragment_identifier, uint8_t protocol) {
	ferr_t status = ferr_ok;
	netman_ipv4_reassembly_identifier_t identifier = {
		.source_address = source_address,
		.destination_address = destination_address,
		.fragment_identifier = fragment_identifier,
		.protocol = protocol,
	};
	netman_ipv4_reassembly_buffer_t* buffer = NULL;

	status = simple_ghmap_lookup(&reassembly_table, &identifier, sizeof(identifier), false, 0, NULL, (void*)&buffer, NULL);
	if (status != ferr_ok) {
		return;
	}

	if (buffer->data) {
		NETMAN_WUR_IGNORE(sys_mempool_free(buffer->data));
	}

	NETMAN_WUR_IGNORE(simple_ghmap_clear(&reassembly_table, &identifier, sizeof(identifier)));
};

static ferr_t netman_ipv4_process_packet(netman_ipv4_packet_t* ip_packet) {
	ferr_t status = ferr_ok;

#if NETMAN_IP_LOG
	sys_console_log_f(
		"Received IPv4 packet: source=%u.%u.%u.%u, dest=%u.%u.%u.%u, length=%zu bytes, protocol=%u\n",
		NETMAN_IPV4_OCTET_A(ip_packet->source_address),
		NETMAN_IPV4_OCTET_B(ip_packet->source_address),
		NETMAN_IPV4_OCTET_C(ip_packet->source_address),
		NETMAN_IPV4_OCTET_D(ip_packet->source_address),
		NETMAN_IPV4_OCTET_A(ip_packet->destination_address),
		NETMAN_IPV4_OCTET_B(ip_packet->destination_address),
		NETMAN_IPV4_OCTET_C(ip_packet->destination_address),
		NETMAN_IPV4_OCTET_D(ip_packet->destination_address),
		ip_packet->length,
		ip_packet->protocol
	);
#endif

	if (ip_packet->protocol == netman_ipv4_protocol_type_icmp) {
		status = netman_icmp_handle_packet(ip_packet);
	} else if (ip_packet->protocol == netman_ipv4_protocol_type_udp) {
		status = netman_udp_handle_packet(ip_packet);
	} else if (ip_packet->protocol == netman_ipv4_protocol_type_tcp) {
		status = netman_tcp_handle_packet(ip_packet);
	} else {
		// we don't know how to handle this packet
		status = ferr_unknown;
	}

	return status;
};

//
// public API
//

void netman_ipv4_packet_destroy(netman_ipv4_packet_t* ip_packet) {
	if (ip_packet->data) {
		NETMAN_WUR_IGNORE(sys_mempool_free(ip_packet->data));
	}
	if (ip_packet->packet) {
		netman_packet_destroy(ip_packet->packet);
	}
	NETMAN_WUR_IGNORE(sys_mempool_free(ip_packet));
};

ferr_t netman_ipv4_handle_packet(netman_packet_t* packet, size_t payload_offset) {
	ferr_t status = ferr_ok;
	void* mapped = NULL;
	netman_ipv4_header_t* header = NULL;
	size_t contained_offset = 0;
	size_t contained_length = 0;

	if (netman_packet_length(packet) - payload_offset < sizeof(netman_ipv4_header_t)) {
		// not our packet
		status = ferr_unknown;
		goto out;
	}

	status = netman_packet_map(packet, &mapped, NULL);
	if (status != ferr_ok) {
		goto out;
	}

	header = (void*)((char*)mapped + payload_offset);

	contained_offset = payload_offset + netman_ipv4_header_ihl(header) * 4;
	contained_length = ferro_byteswap_big_to_native_u16(header->total_length) - (netman_ipv4_header_ihl(header) * 4);

	if (contained_length > (netman_packet_length(packet) - contained_offset)) {
		sys_console_log_f("Bad IPv4 packet: contained length (%zu) is greater than packet content length (%zu)\n", contained_length, netman_packet_length(packet) - contained_offset);
		status = ferr_too_small;
		goto out;
	}

	uint32_t source_address = ferro_byteswap_big_to_native_u32(header->source_address);
	uint32_t destination_address = ferro_byteswap_big_to_native_u32(header->destination_address);
	uint16_t fragment_identifier = ferro_byteswap_big_to_native_u16(header->identification);

	if (netman_ipv4_header_fragment_offset(header) == 0 && (netman_ipv4_header_flags(header) & netman_ipv4_flag_more_fragments) == 0) {
		netman_ipv4_packet_t* ip_packet = NULL;

		netman_ipv4_reassembly_buffer_clear(source_address, destination_address, fragment_identifier, header->protocol);

		status = sys_mempool_allocate(sizeof(*ip_packet), NULL, (void*)&ip_packet);
		if (status != ferr_ok) {
			goto out;
		}

		simple_memset(ip_packet, 0, sizeof(*ip_packet));

		status = sys_mempool_allocate(contained_length, NULL, &ip_packet->data);
		if (status != ferr_ok) {
			NETMAN_WUR_IGNORE(sys_mempool_free(ip_packet));
			goto out;
		}

		// beyond this point, we no longer fail.
		// even if we fail to process the IPv4 packet, we still report success to our caller,
		// which tells them that we've retained ownership of the packet and we'll clean it up.

		if (netman_ether_packet_get_source_mac(packet, &ip_packet->source_mac[0]) == ferr_ok) {
			ip_packet->has_source_mac = true;
		}

		if (netman_ether_packet_get_destination_mac(packet, &ip_packet->destination_mac[0]) == ferr_ok) {
			ip_packet->has_destination_mac = true;
		}

		ip_packet->source_address = source_address;
		ip_packet->destination_address = destination_address;
		ip_packet->protocol = header->protocol;
		ip_packet->length = contained_length;

		simple_memcpy(ip_packet->data, (char*)mapped + contained_offset, netman_packet_length(packet) - contained_offset);

		if (netman_ipv4_process_packet(ip_packet) != ferr_ok) {
			netman_ipv4_packet_destroy(ip_packet);
		}
	} else {
		netman_ipv4_reassembly_buffer_t* buffer = NULL;

		status = netman_ipv4_reassembly_buffer_lookup(source_address, destination_address, fragment_identifier, header->protocol, &buffer);
		if (status != ferr_ok) {
			goto out;
		}

		size_t required_data_length = netman_ipv4_header_fragment_offset(header) * 8 + contained_length;

		if (required_data_length > buffer->length) {
			status = sys_mempool_reallocate(buffer->data, required_data_length, NULL, &buffer->data);
			if (status != ferr_ok) {
				// drop the packet
				netman_ipv4_reassembly_buffer_clear(source_address, destination_address, fragment_identifier, header->protocol);
				goto out;
			}

			buffer->length = required_data_length;
		}

		if ((netman_ipv4_header_flags(header) & netman_ipv4_flag_more_fragments) == 0) {
			buffer->received_end = true;
		}

		simple_memcpy((char*)buffer->data + netman_ipv4_header_fragment_offset(header) * 8, (char*)mapped + contained_offset, contained_length);

		buffer->received_length += contained_length;

		if (buffer->received_end && buffer->length == buffer->received_length) {
			netman_ipv4_packet_t* ip_packet = NULL;

			status = sys_mempool_allocate(sizeof(*ip_packet), NULL, (void*)&ip_packet);
			if (status != ferr_ok) {
				// drop the packet
				netman_ipv4_reassembly_buffer_clear(source_address, destination_address, fragment_identifier, header->protocol);
				goto out;
			}

			simple_memset(ip_packet, 0, sizeof(*ip_packet));

			if (netman_ether_packet_get_source_mac(packet, &ip_packet->source_mac[0]) == ferr_ok) {
				ip_packet->has_source_mac = true;
			}

			if (netman_ether_packet_get_destination_mac(packet, &ip_packet->destination_mac[0]) == ferr_ok) {
				ip_packet->has_destination_mac = true;
			}

			ip_packet->source_address = source_address;
			ip_packet->destination_address = destination_address;
			ip_packet->protocol = header->protocol;
			ip_packet->length = buffer->length;

			ip_packet->data = buffer->data;
			buffer->data = NULL;

			// we don't need the reassembly buffer anymore
			netman_ipv4_reassembly_buffer_clear(source_address, destination_address, fragment_identifier, header->protocol);

			if (netman_ipv4_process_packet(ip_packet) != ferr_ok) {
				netman_ipv4_packet_destroy(ip_packet);
			}
		}
	}

out:
	if (status == ferr_ok) {
		netman_packet_destroy(packet);
	}
	return status;
};

ferr_t netman_ipv4_packet_create(netman_ipv4_packet_t** out_ip_packet) {
	ferr_t status = ferr_ok;
	netman_ipv4_packet_t* ip_packet = NULL;
	void* mapped = NULL;
	netman_ipv4_header_t* header = NULL;

	status = sys_mempool_allocate(sizeof(*ip_packet), NULL, (void*)&ip_packet);
	if (status != ferr_ok) {
		goto out;
	}

	simple_memset(ip_packet, 0, sizeof(*ip_packet));

	status = netman_packet_create(&ip_packet->packet);
	if (status != ferr_ok) {
		goto out;
	}

	status = netman_packet_extend(ip_packet->packet, netman_ether_required_packet_size(sizeof(netman_ipv4_header_t)), false, NULL);
	if (status != ferr_ok) {
		goto out;
	}

	status = netman_ether_packet_write_header(ip_packet->packet, netman_ether_broadcast_address, netman_ether_broadcast_address, netman_ether_packet_type_ipv4, &ip_packet->packet_header_offset);
	if (status != ferr_ok) {
		goto out;
	}

	status = netman_packet_map(ip_packet->packet, &mapped, NULL);
	if (status != ferr_ok) {
		goto out;
	}

	header = (void*)((char*)mapped + ip_packet->packet_header_offset);

	simple_memset(header, 0, sizeof(header));

	header->version_and_ihl = (4 << 4) | (5 << 0);
	header->ttl = 64;

out:
	if (status == ferr_ok) {
		*out_ip_packet = ip_packet;
	} else {
		if (ip_packet) {
			if (ip_packet->packet) {
				netman_packet_destroy(ip_packet->packet);
			}
			NETMAN_WUR_IGNORE(sys_mempool_free(ip_packet));
		}
	}
	return status;
};

ferr_t netman_ipv4_packet_set_destination_address(netman_ipv4_packet_t* ip_packet, uint32_t destination_address) {
	ferr_t status = ferr_ok;
	void* mapped = NULL;
	netman_ipv4_header_t* header = NULL;

	if (!ip_packet->explicit_destination_mac) {
		status = netman_arp_lookup_ipv4(destination_address, &ip_packet->destination_mac[0]);
		if (status != ferr_ok) {
			goto out;
		}

		status = netman_ether_packet_set_destination_mac(ip_packet->packet, &ip_packet->destination_mac[0]);
		if (status != ferr_ok) {
			goto out;
		}

		ip_packet->has_destination_mac = true;
	}

	status = netman_packet_map(ip_packet->packet, &mapped, NULL);
	if (status != ferr_ok) {
		goto out;
	}

	header = (void*)((char*)mapped + ip_packet->packet_header_offset);

	header->destination_address = ferro_byteswap_native_to_big_u32(destination_address);
	ip_packet->destination_address = destination_address;

out:
	return status;
};

ferr_t netman_ipv4_packet_set_destination_mac(netman_ipv4_packet_t* ip_packet, const uint8_t* destination_mac) {
	ferr_t status = ferr_ok;

	if (!destination_mac) {
		ip_packet->has_destination_mac = false;
		ip_packet->explicit_destination_mac = false;
		status = ferr_ok;
		goto out;
	}

	status = netman_ether_packet_set_destination_mac(ip_packet->packet, destination_mac);
	if (status != ferr_ok) {
		goto out;
	}

	simple_memcpy(&ip_packet->destination_mac[0], destination_mac, 6);
	ip_packet->has_destination_mac = true;
	ip_packet->explicit_destination_mac = true;

out:
	return status;
};

ferr_t netman_ipv4_packet_set_protocol(netman_ipv4_packet_t* ip_packet, netman_ipv4_protocol_type_t protocol) {
	ferr_t status = ferr_ok;
	void* mapped = NULL;
	netman_ipv4_header_t* header = NULL;

	status = netman_packet_map(ip_packet->packet, &mapped, NULL);
	if (status != ferr_ok) {
		goto out;
	}

	header = (void*)((char*)mapped + ip_packet->packet_header_offset);

	header->protocol = protocol;
	ip_packet->protocol = protocol;

out:
	return status;
};

ferr_t netman_ipv4_packet_map(netman_ipv4_packet_t* ip_packet, void** out_mapped, size_t* out_length) {
	ferr_t status = ferr_ok;

	if (!out_mapped) {
		status = ferr_invalid_argument;
		goto out;
	}

	if (ip_packet->data) {
		*out_mapped = ip_packet->data;
		if (out_length) {
			*out_length = ip_packet->length;
		}
	} else {
		void* mapped = NULL;
		netman_ipv4_header_t* header = NULL;
		size_t length = 0;

		status = netman_packet_map(ip_packet->packet, &mapped, &length);
		if (status != ferr_ok) {
			goto out;
		}

		header = (void*)((char*)mapped + ip_packet->packet_header_offset);

		*out_mapped = (void*)((char*)mapped + ip_packet->packet_header_offset + netman_ipv4_header_ihl(header) * 4);
		if (out_length) {
			*out_length = length - (ip_packet->packet_header_offset + netman_ipv4_header_ihl(header) * 4);
		}
	}

out:
	return status;
};

size_t netman_ipv4_packet_length(netman_ipv4_packet_t* ip_packet) {
	if (ip_packet->data) {
		return ip_packet->length;
	} else {
		void* mapped = NULL;
		netman_ipv4_header_t* header = NULL;
		size_t length = 0;

		if (netman_packet_map(ip_packet->packet, &mapped, &length) != ferr_ok) {
			return 0;
		}

		header = (void*)((char*)mapped + ip_packet->packet_header_offset);

		return length - (ip_packet->packet_header_offset + netman_ipv4_header_ihl(header) * 4);
	}
};

ferr_t netman_ipv4_packet_append(netman_ipv4_packet_t* ip_packet, const void* data, size_t length, size_t* out_copied) {
	return netman_packet_append(ip_packet->packet, data, length, out_copied);
};

ferr_t netman_ipv4_packet_extend(netman_ipv4_packet_t* ip_packet, size_t length, bool zero, size_t* out_extended) {
	return netman_packet_extend(ip_packet->packet, length, zero, out_extended);
};

static uint16_t netman_ipv4_header_compute_checksum(netman_ipv4_header_t* header) {
	return netman_ipv4_compute_checksum(header, netman_ipv4_header_ihl(header) * 4);
};

ferr_t netman_ipv4_packet_transmit(netman_ipv4_packet_t* ip_packet, netman_device_t* device) {
	ferr_t status = ferr_ok;
	void* mapped = NULL;
	netman_ipv4_header_t* header = NULL;

	status = netman_ether_packet_set_source_mac(ip_packet->packet, netman_device_mac_address(device));
	if (status != ferr_ok) {
		goto out;
	}

	status = netman_packet_map(ip_packet->packet, &mapped, NULL);
	if (status != ferr_ok) {
		goto out;
	}

	header = (void*)((char*)mapped + ip_packet->packet_header_offset);

	// hard-coded for now
	header->source_address = ferro_byteswap_native_to_big_u32(NETMAN_IPV4_STATIC_ADDRESS);

	header->total_length = ferro_byteswap_native_to_big_u16(netman_packet_length(ip_packet->packet) - ip_packet->packet_header_offset);

	header->header_checksum = 0;
	header->header_checksum = netman_ipv4_header_compute_checksum(header);

	status = netman_device_transmit_packet(device, ip_packet->packet, NULL, NULL);
	if (status != ferr_ok) {
		goto out;
	}

	// the device now owns the packet
	ip_packet->packet = NULL;

	// the IPv4 packet has been transmitted successfully, so we can destroy it now
	netman_ipv4_packet_destroy(ip_packet);

out:
	return status;
};

uint32_t netman_ipv4_packet_get_source_address(netman_ipv4_packet_t* ip_packet) {
	return ip_packet->source_address;
};

uint32_t netman_ipv4_packet_get_destination_address(netman_ipv4_packet_t* ip_packet) {
	return ip_packet->destination_address;
};

ferr_t netman_ipv4_packet_get_source_mac(netman_ipv4_packet_t* ip_packet, uint8_t* out_source_mac) {
	if (!ip_packet->has_source_mac) {
		return ferr_no_such_resource;
	}

	simple_memcpy(out_source_mac, &ip_packet->source_mac[0], 6);

	return ferr_ok;
};

ferr_t netman_ipv4_packet_extract_data(netman_ipv4_packet_t* ip_packet, void** out_data, size_t* out_length) {
	ferr_t status = ferr_ok;

	if (!out_data) {
		status = ferr_invalid_argument;
		goto out;
	}

	*out_data = ip_packet->data;
	ip_packet->data = NULL;

	if (out_length) {
		*out_length = ip_packet->length;
	}

out:
	return status;
};
