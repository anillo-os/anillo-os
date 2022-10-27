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

#ifndef _LIBSIMPLE_RING_H_
#define _LIBSIMPLE_RING_H_

#include <libsimple/base.h>
#include <ferro/error.h>

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

LIBSIMPLE_DECLARATIONS_BEGIN;

/**
 * A memory allocation callback for rings.
 *
 * @param context     The callback context given to the ring that is calling this function.
 * @param bytes       The size (in bytes) of how much memory to allocate.
 * @param out_pointer A pointer into which a pointer to the newly allocated memory should be written (upon success).
 *
 * @retval ferr_ok               The allocation succeeded; a pointer to the newly allocated memory has been written into @p out_pointer.
 * @retval ferr_temporary_outage The allocation failed.
 */
typedef ferr_t (*simple_ring_allocate_f)(void* context, size_t bytes, void** out_pointer);

/**
 * A memory release callback for rings.
 *
 * @param context The callback context given to the ring that is calling this function.
 * @param pointer A pointer to the start of the memory that was allocated.
 * @param bytes   The size of the memory (in bytes) that was allocated.
 */
typedef void (*simple_ring_free_f)(void* context, void* pointer, size_t bytes);

LIBSIMPLE_STRUCT(simple_ring) {
	simple_ring_allocate_f allocate;
	simple_ring_free_f free;
	void* callback_context;

	bool using_static_buffer;

	size_t head;
	size_t tail;
	bool full;

	size_t element_size;
	size_t element_count;

	void* elements;
};

LIBSIMPLE_OPTIONS(uint64_t, simple_ring_flags) {
	simple_ring_flag_dynamic = 1 << 0,
};

LIBSIMPLE_WUR ferr_t simple_ring_init(simple_ring_t* ring, size_t element_size, size_t element_count, void* initial_buffer, simple_ring_allocate_f allocate, simple_ring_free_f free, void* callback_context, simple_ring_flags_t flags);
void simple_ring_destroy(simple_ring_t* ring);
LIBSIMPLE_WUR size_t simple_ring_enqueue(simple_ring_t* ring, const void* elements, size_t count);
LIBSIMPLE_WUR size_t simple_ring_dequeue(simple_ring_t* ring, void* out_elements, size_t count);
LIBSIMPLE_WUR size_t simple_ring_peek(simple_ring_t* ring, void* out_elements, size_t count);
size_t simple_ring_queued_count(simple_ring_t* ring);

LIBSIMPLE_DECLARATIONS_END;

#endif // _LIBSIMPLE_RING_H_
