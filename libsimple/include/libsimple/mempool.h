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

#ifndef _LIBSIMPLE_MEMPOOL_H_
#define _LIBSIMPLE_MEMPOOL_H_

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include <libsimple/base.h>
#include <ferro/error.h>

LIBSIMPLE_DECLARATIONS_BEGIN;

// pointer value returned for allocations of size 0
#define LIBSIMPLE_MEMPOOL_NO_BYTES_POINTER ((void*)UINTPTR_MAX)

typedef ferr_t (*simple_mempool_allocator_allocate_f)(void* context, size_t page_count, uint8_t alignment_power, uint8_t boundary_alignment_power, void** out_allocated_start);
typedef ferr_t (*simple_mempool_allocator_free_f)(void* context, size_t page_count, void* allocated_start);
typedef ferr_t (*simple_mempool_allocator_allocate_header_f)(void* context, size_t page_count, void** out_allocated_start);
typedef ferr_t (*simple_mempool_allocator_free_header_f)(void* context, size_t page_count, void* allocated_start);
typedef bool (*simple_mempool_allocator_is_aligned_f)(void* context, void* address, size_t byte_count, uint8_t alignment_power, uint8_t boundary_alignment_power);
typedef LIBSIMPLE_NO_RETURN void (*simple_mempool_panic_f)(const char* message, ...);
typedef void (*simple_mempool_allocator_poison_f)(uintptr_t address, size_t size);
typedef void (*simple_mempool_allocator_unpoison_f)(uintptr_t address, size_t size);

LIBSIMPLE_STRUCT(simple_mempool_allocator) {
	simple_mempool_allocator_allocate_f allocate;
	simple_mempool_allocator_free_f free;
	simple_mempool_allocator_allocate_header_f allocate_header;
	simple_mempool_allocator_free_header_f free_header;
	simple_mempool_allocator_is_aligned_f is_aligned;
	simple_mempool_panic_f panic;
	simple_mempool_allocator_poison_f poison;
	simple_mempool_allocator_unpoison_f unpoison;
};

LIBSIMPLE_STRUCT_FWD(simple_mempool_instance);

LIBSIMPLE_STRUCT(simple_mempool_region_header) {
	simple_mempool_instance_t* instance;
	simple_mempool_region_header_t** prev;
	simple_mempool_region_header_t* next;
	size_t leaf_count;
	size_t free_count;
	void* start;

	// this data is shared between the bucket array and the bitmap
	char data[];
};

LIBSIMPLE_STRUCT(simple_mempool_instance_options) {
	size_t page_size;
	size_t max_order;
	size_t min_leaf_size;
	size_t min_leaf_alignment;
	size_t max_kept_region_count;
	size_t optimal_min_region_order;
};

LIBSIMPLE_STRUCT(simple_mempool_instance) {
	void* context;
	simple_mempool_allocator_t allocator;
	simple_mempool_instance_options_t options;
	simple_mempool_region_header_t* regions_head;
};

LIBSIMPLE_WUR ferr_t simple_mempool_instance_init(simple_mempool_instance_t* instance, void* context, const simple_mempool_allocator_t* allocator, const simple_mempool_instance_options_t* options);

LIBSIMPLE_WUR ferr_t simple_mempool_instance_destroy(simple_mempool_instance_t* instance);

/**
 * Allocates a region of kernel memory of the given size.
 *
 * @param                      instance The memory pool instance to allocate from.
 * @param                    byte_count The number of bytes to allocate in the memory block. The actual number of bytes allocated may be greater than this value (but never less). `0` is a valid value for this parameter.
 * @param               alignment_power A power of two for the alignment that the allocated region should have.
 *                                      For example, for 8-byte alignment, this should be 3 because 2^3 = 8.
 *                                      A value of 0 is 2^0 = 1, which is normal, unaligned memory.
 * @param      boundary_alignment_power A power of two for the alignment of the boundary that the allocated region must not cross.
 *                                      The usage of this value is identical to that of @p alignment_power (e.g. a value of 3
 *                                      would result in a boundary alignment of 8 bytes).
 *                                      A value greater than 63 results in having no boundary alignment requirement.
 *                                      Note that `0` is a valid (yet absurd) value for this and does impose a 1-byte boundary alignment requirement.
 * @param[out] out_allocated_byte_count Optional pointer in which to store the number of bytes actually allocated.
 * @param[out]      out_allocated_start Pointer in which to store a pointer to the start address of the allocated region.
 *
 * @note Alignment and boundary alignment are not the same thing: alignment ensures that the region starts on a memory address
 *       with the given alignment, whereas boundary alignment ensures that the region does not cross a memory address with the given alignment.
 *       For example, allocating 48 bytes with a 16-byte alignment and no boundary alignment may result in the memory address 0xe0.
 *       However, allocating the same region with a 64-byte boundary alignment could not result in a region starting at 0xe0 because
 *       this region would cross a 64-byte boundary at 0x100. Additionally, allocating 48 bytes with no alignment and a 64-byte boundary alignment
 *       could result in a region starting at 0xc4, which is only 4-byte aligned but does not cross a 64-byte boundary.
 *
 * @retval ferr_ok               The region has been allocated.
 * @retval ferr_invalid_argument One or more of the following: 1) @p byte_count was invalid (e.g. `SIZE_MAX`), 2) @p out_allocated_start was NULL, 3) @p alignment_power was an invalid power (e.g. larger than `63`).
 * @retval ferr_temporary_outage There was not enough memory available to satisfy the request.
 */
LIBSIMPLE_WUR ferr_t simple_mempool_allocate(simple_mempool_instance_t* instance, size_t byte_count, uint8_t alignment_power, uint8_t boundary_alignment_power, size_t* out_allocated_byte_count, void** out_allocated_start);

/**
 * Reallocates a region of kernel memory to a new size.
 *
 * @param                        instance The memory pool instance that was used to allocate @p old_address and that will be used to allocate the new region from.
 * @param                     old_address The start address of the region to resize. It is valid to pass `NULL` for this, in which case the function behaves exactly like simple_mempool_allocate().
 * @param                  new_byte_count The number of bytes to allocate in the new memory block. The actual number of bytes allocated may be greater than this value (but never less). `0` is a valid value for this parameter.
 * @param                 alignment_power See simple_mempool_allocate().
 * @param        boundary_alignment_power See simple_mempool_allocate().
 * @param[out] out_reallocated_byte_count Optional pointer in which to store the number of bytes actually allocated.
 * @param[out]      out_reallocated_start Pointer in which to store a pointer to the start address of the reallocated region.
 *
 * @retval ferr_ok               The region has been reallocated, possibly to a different address.
 * @retval ferr_invalid_argument One or more of the following: 1) @p old_address was invalid (e.g. not previously allocated), 2) @p new_byte_count was invalid (e.g. `SIZE_MAX`), 3) @p out_reallocated_start was NULL, 3) @p alignment_power was an invalid power (e.g. larger than `63`).
 * @retval ferr_temporary_outage There was not enough memory available to satisfy the request. The existing memory remains untouched.
 */
LIBSIMPLE_WUR ferr_t simple_mempool_reallocate(simple_mempool_instance_t* instance, void* old_address, size_t new_byte_count, uint8_t alignment_power, uint8_t boundary_alignment_power, size_t* out_reallocated_byte_count, void** out_reallocated_start);

/**
 * Frees a region of memory previously allocated with simple_mempool_allocate().
 *
 * @param instance The memory pool instance that was used to allocate the memory.
 * @param address  The start address of the region to free.
 *
 * Return values:
 * @retval ferr_ok               The region has been freed.
 * @retval ferr_invalid_argument @p address was invalid (e.g. `NULL` or not previously allocated).
 */
LIBSIMPLE_WUR ferr_t simple_mempool_free(simple_mempool_instance_t* instance, void* address);

bool simple_mempool_get_allocated_byte_count(simple_mempool_instance_t* instance, void* address, size_t* out_allocated_byte_count);

LIBSIMPLE_ALWAYS_INLINE bool simple_mempool_belongs_to_instance(simple_mempool_instance_t* instance, void* address) {
	return simple_mempool_get_allocated_byte_count(instance, address, NULL);
};

LIBSIMPLE_DECLARATIONS_END;

#endif // _LIBSIMPLE_MEMPOOL_H_
