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

#include <libsimple/ring.h>
#include <libsimple/general.h>

ferr_t simple_ring_init(simple_ring_t* ring, size_t element_size, size_t element_count, void* initial_buffer, simple_ring_allocate_f allocate, simple_ring_free_f free, void* callback_context, simple_ring_flags_t flags) {
	ferr_t status = ferr_ok;

	if (!initial_buffer && !(allocate && free)) {
		status = ferr_invalid_argument;
		goto out;
	}

	simple_memset(ring, 0, sizeof(*ring));

	ring->allocate = allocate;
	ring->free = free;
	ring->callback_context = callback_context;

	ring->element_size = element_size;
	ring->element_count = element_count;

	if (!initial_buffer) {
		status = ring->allocate(ring->callback_context, element_size * element_count, &initial_buffer);
		if (status != ferr_ok) {
			goto out;
		}
	}

	ring->using_static_buffer = !!initial_buffer;

	ring->elements = initial_buffer;

out:
	return status;
};

void simple_ring_destroy(simple_ring_t* ring) {
	if (!ring->using_static_buffer) {
		ring->free(ring->callback_context, ring->elements, ring->element_size * ring->element_count);
	}
};

size_t simple_ring_enqueue(simple_ring_t* ring, const void* elements, size_t count) {
	size_t enqueued = 0;
	const void* elements_end = (const char*)elements + (count * ring->element_size);

	// TODO: we can optimize this by copying multiple elements at a time

	for (const void* element = elements; element < elements_end; element = (const char*)element + ring->element_size) {
		if (ring->full) {
			break;
		}

		simple_memcpy((char*)ring->elements + (ring->element_size * ring->tail), element, ring->element_size);

		ring->tail = (ring->tail + 1) % ring->element_count;

		if (ring->tail == ring->head) {
			ring->full = true;
		}

		++enqueued;
	}

	return enqueued;
};

size_t simple_ring_dequeue(simple_ring_t* ring, void* out_elements, size_t count) {
	size_t dequeued = 0;
	void* out_elements_end = (char*)out_elements + (count * ring->element_size);

	// TODO: same optimization possible as for enqueueing

	for (void* out_element = out_elements; out_element < out_elements_end; out_element = (char*)out_element + ring->element_size) {
		if (ring->head == ring->tail && !ring->full) {
			break;
		}

		simple_memcpy(out_element, (const char*)ring->elements + (ring->element_size * ring->head), ring->element_size);

		ring->head = (ring->head + 1) % ring->element_count;

		ring->full = false;

		++dequeued;
	}

	return dequeued;
};

size_t simple_ring_peek(simple_ring_t* ring, void* out_elements, size_t count) {
	bool orig_full = ring->full;
	size_t orig_head = ring->head;
	size_t peeked = simple_ring_dequeue(ring, out_elements, count);

	ring->full = orig_full;
	ring->head = orig_head;

	return peeked;
};

size_t simple_ring_queued_count(simple_ring_t* ring) {
	if (ring->full) {
		return ring->element_count;
	}

	if (ring->head <= ring->tail) {
		return ring->tail - ring->head;
	} else {
		return ring->element_count - (ring->head - ring->tail);
	}
};
