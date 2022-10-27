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

#include <netman/packet.private.h>
#include <libsys/libsys.h>
#include <libsimple/libsimple.h>

// FIXME: the way this is implemented is memory inefficient for outgoing packets.

ferr_t netman_packet_create(netman_packet_t** out_packet) {
	ferr_t status = ferr_ok;
	netman_packet_t* packet = NULL;

	if (!out_packet) {
		status = ferr_invalid_argument;
		goto out;
	}

	status = sys_mempool_allocate(sizeof(netman_packet_t), NULL, (void*)&packet);
	if (status != ferr_ok) {
		goto out;
	}

	simple_memset(packet, 0, sizeof(*packet));

out:
	if (status == ferr_ok) {
		*out_packet = packet;
	}
	return status;
};

size_t netman_packet_length(netman_packet_t* packet) {
	return packet->total_length;
};

ferr_t netman_packet_append(netman_packet_t* packet, const void* data, size_t length, size_t* out_copied) {
	ferr_t status = ferr_ok;
	size_t copied = 0;

	if (!data || length == 0 || length == SIZE_MAX) {
		status = ferr_invalid_argument;
		goto out;
	}

	size_t space_in_last_page = sys_page_round_up_multiple(packet->total_length) - packet->total_length;
	size_t init_copy_len = (space_in_last_page < length) ? space_in_last_page : length;
	size_t required_pages = sys_page_round_up_count(length - init_copy_len);
	const void* data_after_init_copy = (const char*)data + init_copy_len;

	size_t orig_len = packet->total_length;

	simple_memcpy(&packet->last_page_mapping[packet->total_length - sys_page_round_down_multiple(packet->total_length)], data, init_copy_len);
	packet->total_length += init_copy_len;
	copied += init_copy_len;
	if (packet->buffer_count > 0) {
		packet->buffers[packet->buffer_count - 1].length += init_copy_len;
	}

	if (copied > 0) {
		// the persistent mapping is now invalid
		if (packet->persistent_mapping) {
			NETMAN_WUR_IGNORE(sys_page_free(packet->persistent_mapping));
			packet->persistent_mapping = NULL;
		}
	}

	size_t remaining_length = length - init_copy_len;
	const char* curr_data = data_after_init_copy;
	for (size_t i = 0; i < required_pages; ++i) {
		size_t this_len = (remaining_length > 4096) ? 4096 : remaining_length;
		void* virt = NULL;

		// allocate a page
		status = sys_page_allocate(1, sys_page_flag_contiguous | sys_page_flag_prebound | sys_page_flag_uncacheable, &virt);
		if (status != ferr_ok) {
			goto out;
		}

		// append the page
		status = netman_packet_append_no_copy(packet, virt, this_len);
		if (status != ferr_ok) {
			NETMAN_WUR_IGNORE(sys_page_free(virt));
			goto out;
		}

		simple_memcpy(packet->last_page_mapping, curr_data, this_len);

		remaining_length -= this_len;
		curr_data += this_len;
		copied += this_len;
	}

out:
	if (out_copied) {
		*out_copied = copied;
	}
	return status;
};

ferr_t netman_packet_append_no_copy(netman_packet_t* packet, void* data, size_t length) {
	ferr_t status = ferr_ok;

	if (!data || length == 0 || length > 0x2000/* || !fpage_is_page_aligned((uintptr_t)physical_data)*/) {
		status = ferr_invalid_argument;
		goto out;
	}

	// if the packet is non-empty, it must have at least one buffer.
	// additionally, if the packet is non-empty, it must have a stored last-page mapping.
	fassert(packet->total_length > 0 == packet->buffer_count > 0 && packet->total_length > 0 == !!packet->last_page_mapping);

	if (packet->buffer_count == packet->buffer_allocated_count) {
		// allocate an extra buffer entry
		status = sys_mempool_reallocate(packet->buffers, sizeof(netman_packet_buffer_t) * (packet->buffer_count + 1), NULL, (void*)&packet->buffers);
		if (status != ferr_ok) {
			goto out;
		}

		++packet->buffer_allocated_count;
	}

	// zero out the rest of the last page
	// if we don't have one, the length is 0, so this is just a no-op.
	simple_memset(&packet->last_page_mapping[packet->total_length - sys_page_round_down_multiple(packet->total_length)], 0, sys_page_round_up_multiple(packet->total_length) - packet->total_length);

	void* last_page = (char*)data + sys_page_round_down_multiple(length);
	// update the last-page mapping
	packet->last_page_mapping = last_page;
	if (status != ferr_ok) {
		goto out;
	}

	// we cannot fail beyond this point

	// the persistent mapping is now invalid
	if (packet->persistent_mapping) {
		NETMAN_WUR_IGNORE(sys_page_free(packet->persistent_mapping));
		packet->persistent_mapping = NULL;
	}

	// the total length is now page-aligned
	packet->total_length = sys_page_round_up_multiple(packet->total_length);
	if (packet->buffer_count > 0) {
		packet->buffers[packet->buffer_count - 1].length = sys_page_round_up_multiple(packet->buffers[packet->buffer_count - 1].length);
	}

	packet->buffers[packet->buffer_count].address = data;
	packet->buffers[packet->buffer_count].length = length;
	++packet->buffer_count;

	packet->total_length += length;

out:
	return status;
};

ferr_t netman_packet_extend(netman_packet_t* packet, size_t length, bool zero, size_t* out_extended) {
	ferr_t status = ferr_ok;
	size_t extended = 0;

	if (length == 0) {
		status = ferr_invalid_argument;
		goto out;
	}

	size_t space_in_last_page = sys_page_round_up_multiple(packet->total_length) - packet->total_length;
	size_t init_len = (space_in_last_page < length) ? space_in_last_page : length;
	size_t required_pages = sys_page_round_up_count(length - init_len);

	size_t orig_len = packet->total_length;

	if (zero) {
		simple_memset(&packet->last_page_mapping[packet->total_length - sys_page_round_down_multiple(packet->total_length)], 0, init_len);
	}
	packet->total_length += init_len;
	extended += init_len;
	if (packet->buffer_count > 0) {
		packet->buffers[packet->buffer_count - 1].length += init_len;
	}

	if (extended > 0) {
		// the persistent mapping is now invalid
		if (packet->persistent_mapping) {
			NETMAN_WUR_IGNORE(sys_page_free(packet->persistent_mapping));
			packet->persistent_mapping = NULL;
		}
	}

	size_t remaining_length = length - init_len;
	for (size_t i = 0; i < required_pages; ++i) {
		size_t this_len = (remaining_length > 4096) ? 4096 : remaining_length;
		void* virt = NULL;

		// allocate a physical page
		status = sys_page_allocate(1, sys_page_flag_contiguous | sys_page_flag_prebound | sys_page_flag_uncacheable, &virt);
		if (status != ferr_ok) {
			goto out;
		}

		// append the page
		status = netman_packet_append_no_copy(packet, virt, this_len);
		if (status != ferr_ok) {
			NETMAN_WUR_IGNORE(sys_page_free(virt));
			goto out;
		}

		if (zero) {
			simple_memset(packet->last_page_mapping, 0, this_len);
		}

		remaining_length -= this_len;
		extended += this_len;
	}

out:
	if (out_extended) {
		*out_extended = extended;
	}
	return status;
};

ferr_t netman_packet_map(netman_packet_t* packet, void** out_data, size_t* out_length) {
	ferr_t status = ferr_ok;
	sys_shared_memory_t* tmp = NULL;

	if (!out_data) {
		status = ferr_invalid_argument;
		goto out;
	}

	if (packet->persistent_mapping) {
		goto out;
	}

	status = sys_shared_memory_allocate(sys_page_round_up_count(packet->total_length), 0, &tmp);
	if (status != ferr_ok) {
		goto out;
	}

	size_t data_page_offset = 0;
	for (size_t i = 0; i < packet->buffer_count; ++i) {
		netman_packet_buffer_t* buffer = &packet->buffers[i];
		status = sys_shared_memory_bind(tmp, sys_page_round_up_count(buffer->length), data_page_offset, buffer->address);
		if (status != ferr_ok) {
			goto out;
		}
		data_page_offset += sys_page_round_up_count(buffer->length);
	}

	status = sys_shared_memory_map(tmp, sys_page_round_up_count(packet->total_length), 0, &packet->persistent_mapping);

out:
	if (tmp) {
		sys_release(tmp);
	}
	if (status == ferr_ok) {
		*out_data = packet->persistent_mapping;
		if (out_length) {
			*out_length = packet->total_length;
		}
	}
	return status;
};

void netman_packet_destroy(netman_packet_t* packet) {
	if (packet->persistent_mapping) {
		NETMAN_WUR_IGNORE(sys_page_free(packet->persistent_mapping));
	}

	for (size_t i = 0; i < packet->buffer_count; ++i) {
		netman_packet_buffer_t* buffer = &packet->buffers[i];
		if (buffer->address) {
			NETMAN_WUR_IGNORE(sys_page_free(buffer->address));
		}
	}

	if (packet->buffers) {
		NETMAN_WUR_IGNORE(sys_mempool_free(packet->buffers));
	}

	NETMAN_WUR_IGNORE(sys_mempool_free(packet));
};
