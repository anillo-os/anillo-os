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
#include <libsimple/libsimple.h>
#include <libsimple/libsimple.h>
#include <netman/packet.h>
#include <netman/device.h>
#include <ferro/byteswap.h>
#include <netman/ip.h>

#ifndef NETMAN_ARP_LOG
	#define NETMAN_ARP_LOG 0
#endif

NETMAN_STRUCT(netman_arp_entry) {
	bool valid;
	uint8_t mac[6];
};

NETMAN_PACKED_STRUCT(netman_arp_header) {
	uint16_t hardware_type;
	netman_ether_packet_type_t protocol_type;
	uint8_t hardware_address_length;
	uint8_t protocol_address_length;
	uint16_t operation;
};

NETMAN_PACKED_STRUCT(netman_arp_ipv4) {
	netman_arp_header_t header;
	uint8_t sender_mac[6];
	uint32_t sender_ip_address;
	uint8_t target_mac[6];
	uint32_t target_ip_address;
};

NETMAN_STRUCT(netman_arp_protocol_table) {
	simple_ghmap_t table;
	sys_mutex_t lock;
};

static simple_ghmap_t arp_table;
static sys_mutex_t arp_table_lock = SYS_MUTEX_INIT;

void netman_arp_init(void) {
	sys_abort_status_log(simple_ghmap_init(&arp_table, 2, 0, simple_ghmap_allocate_sys_mempool, simple_ghmap_free_sys_mempool, NULL, NULL, NULL, NULL, NULL, NULL));
};

static ferr_t netman_arp_create_packet_ipv4(netman_device_t* device, const uint8_t* sender_mac, uint32_t sender_address, const uint8_t* target_mac, uint32_t target_address, netman_packet_t** out_packet) {
	ferr_t status = ferr_ok;
	netman_packet_t* packet = NULL;
	void* mapped = NULL;
	netman_arp_ipv4_t* payload = NULL;
	size_t offset = 0;

	status = netman_packet_create(&packet);
	if (status != ferr_ok) {
		goto out;
	}

	status = netman_packet_extend(packet, netman_ether_required_packet_size(sizeof(netman_arp_ipv4_t)), false, NULL);
	if (status != ferr_ok) {
		goto out;
	}

	status = netman_ether_packet_write_header(packet, netman_device_mac_address(device), target_mac ? target_mac : netman_ether_broadcast_address, netman_ether_packet_type_arp, &offset);
	if (status != ferr_ok) {
		goto out;
	}

	status = netman_packet_map(packet, &mapped, NULL);
	if (status != ferr_ok) {
		goto out;
	}

	payload = (void*)((char*)mapped + offset);

	payload->header.hardware_type = ferro_byteswap_native_to_big_u16(1);
	payload->header.protocol_type = ferro_byteswap_native_to_big_u16(netman_ether_packet_type_ipv4);
	payload->header.hardware_address_length = 6;
	payload->header.protocol_address_length = 4;
	payload->header.operation = ferro_byteswap_native_to_big_u16(target_mac ? 2 : 1);

	simple_memcpy(&payload->sender_mac[0], sender_mac, 6);
	payload->sender_ip_address = ferro_byteswap_native_to_big_u32(sender_address);

	if (target_mac) {
		simple_memcpy(&payload->target_mac[0], target_mac, 6);
	} else {
		simple_memset(&payload->target_mac[0], 0, 6);
	}
	payload->target_ip_address = ferro_byteswap_native_to_big_u32(target_address);

out:
	if (status == ferr_ok) {
		*out_packet = packet;
	} else {
		if (packet) {
			netman_packet_destroy(packet);
		}
	}
	return status;
};

static ferr_t netman_arp_lookup_protocol_table(netman_ether_packet_type_t protocol_type, netman_arp_protocol_table_t** out_protocol_table) {
	ferr_t status = ferr_ok;
	netman_arp_protocol_table_t* protocol_table = NULL;
	bool created = false;

	sys_mutex_lock(&arp_table_lock);

	status = simple_ghmap_lookup_h(&arp_table, protocol_type, true, sizeof(netman_arp_protocol_table_t), &created, (void*)&protocol_table, NULL);
	if (status != ferr_ok) {
		goto out;
	}

	if (created) {
		sys_mutex_init(&protocol_table->lock);

		status = simple_ghmap_init(&protocol_table->table, 16, 0, simple_ghmap_allocate_sys_mempool, simple_ghmap_free_sys_mempool, simple_ghmap_hash_data, simple_ghmap_compares_equal_data, NULL, NULL, NULL, NULL);
		if (status != ferr_ok) {
			NETMAN_WUR_IGNORE(simple_ghmap_clear_h(&arp_table, protocol_type));
			goto out;
		}
	}

out:
	sys_mutex_unlock(&arp_table_lock);
out_unlocked:
	if (status == ferr_ok) {
		*out_protocol_table = protocol_table;
	}
	return status;
};

ferr_t netman_arp_handle_packet(netman_packet_t* packet, size_t payload_offset) {
	ferr_t status = ferr_ok;
	void* mapped = NULL;
	netman_arp_ipv4_t* payload = NULL;
	netman_arp_protocol_table_t* protocol_table = NULL;
	netman_arp_entry_t* entry = NULL;
	bool created = false;
	uint32_t sender_ip;

	if (netman_packet_length(packet) - payload_offset < sizeof(netman_arp_ipv4_t)) {
		// not our packet
		status = ferr_unknown;
		goto out;
	}

	status = netman_packet_map(packet, &mapped, NULL);
	if (status != ferr_ok) {
		goto out;
	}

	payload = (void*)((char*)mapped + payload_offset);

	if (
		ferro_byteswap_big_to_native_u16(payload->header.hardware_type) != 1 ||
		ferro_byteswap_big_to_native_u16(payload->header.protocol_type) != netman_ether_packet_type_ipv4 ||
		payload->header.hardware_address_length != 6 ||
		payload->header.protocol_address_length != 4
	) {
		// not our packet
		status = ferr_unknown;
		goto out;
	}

	status = netman_arp_lookup_protocol_table(ferro_byteswap_big_to_native_u16(payload->header.protocol_type), &protocol_table);
	if (status != ferr_ok) {
		goto out;
	}

	sender_ip = ferro_byteswap_big_to_native_u32(payload->sender_ip_address);

	sys_mutex_lock(&protocol_table->lock);

	status = simple_ghmap_lookup(&protocol_table->table, &sender_ip, 4, true, 0, &created, (void*)&entry, NULL);
	if (status != ferr_ok) {
		sys_mutex_unlock(&protocol_table->lock);
		goto out;
	}

	simple_memcpy(&entry->mac[0], &payload->sender_mac[0], 6);
	entry->valid = true;

#if NETMAN_ARP_LOG
	sys_console_log_f(
		"ARP: adding mapping for IPv4 %u.%u.%u.%u -> %02x:%02x:%02x:%02x:%02x:%02x\n",
		(sender_ip >> 24) & 0xff,
		(sender_ip >> 16) & 0xff,
		(sender_ip >>  8) & 0xff,
		(sender_ip >>  0) & 0xff,
		entry->mac[0],
		entry->mac[1],
		entry->mac[2],
		entry->mac[3],
		entry->mac[4],
		entry->mac[5]
	);
#endif

	sys_mutex_unlock(&protocol_table->lock);

	if (ferro_byteswap_big_to_native_u16(payload->header.operation) == 1 && ferro_byteswap_big_to_native_u32(payload->target_ip_address) == NETMAN_IPV4_STATIC_ADDRESS) {
		// try to send a reply.
		// it's okay if we fail, though.
		ferr_t status2 = ferr_ok;
		netman_device_t* device = netman_device_any();
		netman_packet_t* reply_packet = NULL;

#if NETMAN_ARP_LOG
		sys_console_log_f(
			"ARP: %u.%u.%u.%u has asked for our MAC address. Telling them now...\n",
			(sender_ip >> 24) & 0xff,
			(sender_ip >> 16) & 0xff,
			(sender_ip >>  8) & 0xff,
			(sender_ip >>  0) & 0xff
		);
#endif

		status2 = netman_arp_create_packet_ipv4(device, netman_device_mac_address(device), NETMAN_IPV4_STATIC_ADDRESS, &payload->sender_mac[0], sender_ip, &reply_packet);
		if (status2 != ferr_ok) {
			goto reply_out;
		}

		status2 = netman_device_transmit_packet(device, reply_packet, NULL, NULL);

reply_out:;
		if (status2 != ferr_ok) {
			if (reply_packet) {
				netman_packet_destroy(reply_packet);
			}
		}
	}

out:
	if (status == ferr_ok) {
		netman_packet_destroy(packet);
	}
	return status;
};

ferr_t netman_arp_resolve(netman_ether_packet_type_t protocol_type, const uint8_t* protocol_address, size_t protocol_address_length) {
	ferr_t status = ferr_ok;
	netman_packet_t* packet = NULL;
	netman_device_t* device = NULL;

	// TODO: maybe add support for other protocol types?
	//       IPv4 is really the only useful one, though.
	if (protocol_type != netman_ether_packet_type_ipv4) {
		status = ferr_unsupported;
		goto out;
	}

	device = netman_device_any();
	if (!device) {
		status = ferr_temporary_outage;
		goto out;
	}

	status = netman_arp_create_packet_ipv4(device, netman_device_mac_address(device), NETMAN_IPV4_STATIC_ADDRESS, NULL, *(uint32_t*)protocol_address, &packet);
	if (status != ferr_ok) {
		goto out;
	}

	status = netman_device_transmit_packet(device, packet, NULL, NULL);
	if (status != ferr_ok) {
		goto out;
	}

	// the device now owns the packet
	packet = NULL;

out:
	if (status != ferr_ok) {
		if (packet) {
			netman_packet_destroy(packet);
		}
	}
	return status;
};

ferr_t netman_arp_lookup(netman_ether_packet_type_t protocol_type, const uint8_t* protocol_address, size_t protocol_address_length, uint8_t* out_mac) {
	ferr_t status = ferr_ok;
	netman_arp_protocol_table_t* protocol_table = NULL;
	bool created = false;
	netman_arp_entry_t* entry = NULL;

	status = netman_arp_lookup_protocol_table(protocol_type, &protocol_table);
	if (status != ferr_ok) {
		goto out_unlocked;
	}

	sys_mutex_lock(&protocol_table->lock);

	status = simple_ghmap_lookup(&protocol_table->table, protocol_address, protocol_address_length, true, 0, &created, (void*)&entry, NULL);
	if (status != ferr_ok) {
		goto out;
	}

	if (created) {
		// no valid entry in our table; (try to) start an ARP resolution
		status = netman_arp_resolve(protocol_type, protocol_address, protocol_address_length);
		if (status == ferr_ok) {
			entry->valid = false;
			status = ferr_should_restart;
		} else {
			NETMAN_WUR_IGNORE(simple_ghmap_clear(&protocol_table->table, protocol_address, protocol_address_length));
		}

		goto out;
	}

	if (!entry->valid) {
		// the entry exists, but isn't valid yet.
		// an ARP resolution is already in progress.
		status = ferr_should_restart;
		goto out;
	}

	if (out_mac) {
		simple_memcpy(out_mac, entry->mac, 6);
	}

out:
	sys_mutex_unlock(&protocol_table->lock);
out_unlocked:
	return status;
};

ferr_t netman_arp_lookup_ipv4(uint32_t ip_address, uint8_t* out_mac) {
	return netman_arp_lookup(netman_ether_packet_type_ipv4, (void*)&ip_address, sizeof(ip_address), out_mac);
};

ferr_t netman_arp_register(netman_ether_packet_type_t protocol_type, const uint8_t* protocol_address, size_t protocol_address_length, const uint8_t* mac) {
	ferr_t status = ferr_ok;
	netman_arp_protocol_table_t* protocol_table = NULL;
	bool created = false;
	netman_arp_entry_t* entry = NULL;

	status = netman_arp_lookup_protocol_table(protocol_type, &protocol_table);
	if (status != ferr_ok) {
		goto out_unlocked;
	}

	sys_mutex_lock(&protocol_table->lock);

	status = simple_ghmap_lookup(&protocol_table->table, protocol_address, protocol_address_length, true, 0, &created, (void*)&entry, NULL);
	if (status != ferr_ok) {
		goto out;
	}

	simple_memcpy(&entry->mac[0], mac, 6);
	entry->valid = true;

out:
	sys_mutex_unlock(&protocol_table->lock);
out_unlocked:
	return status;
};

ferr_t netman_arp_register_ipv4(uint32_t ip_address, const uint8_t* mac) {
	return netman_arp_register(netman_ether_packet_type_ipv4, (void*)&ip_address, sizeof(ip_address), mac);
};
