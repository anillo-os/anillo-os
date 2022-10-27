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

#ifndef _NETMAN_IP_PRIVATE_H_
#define _NETMAN_IP_PRIVATE_H_

#include <netman/ip.h>
#include <ferro/byteswap.h>

#include <stdbool.h>

NETMAN_DECLARATIONS_BEGIN;

NETMAN_PACKED_STRUCT(netman_ipv4_header) {
	uint8_t version_and_ihl;
	uint8_t type_of_service;
	uint16_t total_length;
	uint16_t identification;
	uint16_t flags_and_fragment_offset;
	uint8_t ttl;
	uint8_t protocol;
	uint16_t header_checksum;
	uint32_t source_address;
	uint32_t destination_address;
};

NETMAN_ENUM(uint16_t, netman_ipv4_flags) {
	netman_ipv4_flag_dont_fragment  = 1 << 1,
	netman_ipv4_flag_more_fragments = 1 << 0,
};

NETMAN_STRUCT(netman_ipv4_reassembly_identifier) {
	uint32_t source_address;
	uint32_t destination_address;
	uint16_t fragment_identifier;
	uint8_t protocol;
};

NETMAN_STRUCT(netman_ipv4_reassembly_buffer) {
	const netman_ipv4_reassembly_identifier_t* identifier;
	void* data;
	size_t length;
	size_t received_length;
	bool received_end;

	// TODO: timeouts
	//fwork_t* timeout_work;
};

NETMAN_STRUCT(netman_ipv4_packet) {
	uint8_t source_mac[6];
	uint8_t destination_mac[6];
	uint32_t source_address;
	uint32_t destination_address;
	netman_ipv4_protocol_type_t protocol;
	bool has_source_mac;
	bool has_destination_mac;
	bool explicit_destination_mac;

	void* data;
	size_t length;

	netman_packet_t* packet;
	size_t packet_header_offset;
};

NETMAN_ALWAYS_INLINE
uint16_t netman_ipv4_header_fragment_offset(netman_ipv4_header_t* header) {
	return ferro_byteswap_big_to_native_u16(header->flags_and_fragment_offset) & 0x1fff;
};

NETMAN_ALWAYS_INLINE
uint8_t netman_ipv4_header_ihl(netman_ipv4_header_t* header) {
	return header->version_and_ihl & 0x0f;
};

NETMAN_ALWAYS_INLINE
uint8_t netman_ipv4_header_version(netman_ipv4_header_t* header) {
	return header->version_and_ihl >> 4;
};

NETMAN_ALWAYS_INLINE
uint8_t netman_ipv4_header_flags(netman_ipv4_header_t* header) {
	return ferro_byteswap_big_to_native_u16(header->flags_and_fragment_offset) >> 13;
};

NETMAN_STRUCT(netman_ipv4_checksum_state) {
	uint32_t checksum;
	bool odd_length;
	uint8_t trailing_byte;
};

NETMAN_ALWAYS_INLINE
void netman_ipv4_checksum_init(netman_ipv4_checksum_state_t* state) {
	state->checksum = 0;
	state->odd_length = false;
	state->trailing_byte = 0;
};

NETMAN_ALWAYS_INLINE
void netman_ipv4_checksum_add(netman_ipv4_checksum_state_t* state, const void* data, size_t data_length) {
	if (state->odd_length && data_length > 0) {
		state->checksum += ((uint16_t)((const uint8_t*)data)[0] << 8) | (uint16_t)state->trailing_byte;
		--data_length;
		data = (const char*)data + 1;
		state->odd_length = false;
	}

	for (size_t i = 0; i < data_length / 2; ++i) {
		state->checksum += ((const uint16_t*)data)[i];
	}

	if ((data_length & 1) != 0) {
		state->trailing_byte = ((const uint8_t*)data)[data_length - 1];
		state->odd_length = true;
	}
};

NETMAN_ALWAYS_INLINE
uint16_t netman_ipv4_checksum_finish(netman_ipv4_checksum_state_t* state) {
	if (state->odd_length) {
		// odd length; add in the final byte
		state->checksum += (uint16_t)state->trailing_byte;
	}

	// take care of any carries by adding them back in
	state->checksum = (state->checksum & 0xffff) + (state->checksum >> 16);

	// do it again in case the first carry addition created an additional carry
	state->checksum = (state->checksum & 0xffff) + (state->checksum >> 16);

	// we should only ever need to do it twice

	return ~(uint16_t)(state->checksum & 0xffff);
};

/**
 * Produces a 16-bit one's compliment checksum, according to RFC 1071.
 *
 * The checksum produced does NOT need to be byte-swapped.
 */
NETMAN_ALWAYS_INLINE
uint16_t netman_ipv4_compute_checksum(const void* data, size_t data_length) {
	netman_ipv4_checksum_state_t state;
	netman_ipv4_checksum_init(&state);
	netman_ipv4_checksum_add(&state, data, data_length);
	return netman_ipv4_checksum_finish(&state);
};

NETMAN_DECLARATIONS_END;

#endif // _NETMAN_NET_IP_PRIVATE_H_
