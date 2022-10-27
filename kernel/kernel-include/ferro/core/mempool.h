/*
 * This file is part of Anillo OS
 * Copyright (C) 2021 Anillo OS Developers
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

/**
 * @file
 *
 * Memory pool subsystem.
 */

#ifndef _FERRO_CORE_MEMPOOL_H_
#define _FERRO_CORE_MEMPOOL_H_

#include <stddef.h>
#include <stdint.h>

#include <ferro/base.h>
#include <ferro/error.h>

FERRO_DECLARATIONS_BEGIN;

/**
 * @addtogroup Memory-Pool
 *
 * The memory pool subsystem.
 *
 * @{
 */

FERRO_OPTIONS(uint64_t, fmempool_flags) {
	/**
	 * Allocated memory must be physically contiguous.
	 */
	fmempool_flag_physically_contiguous = 1 << 0,

	/**
	 * Allocated must be prebound (i.e. it will not generate faults).
	 *
	 * Currently cannot be used together with ::fmempool_flag_physically_contiguous.
	 */
	fmempool_flag_prebound = 1 << 1,
};

void fmempool_init(void);

/**
 * Allocates a region of kernel memory of the given size.
 *
 * @param                    byte_count The number of bytes to allocate in the memory block. The actual number of bytes allocated may be greater than this value (but never less). `0` is a valid value for this parameter.
 * @param[out] out_allocated_byte_count Optional pointer in which to store the number of bytes actually allocated.
 * @param[out]      out_allocated_start Pointer in which to store a pointer to the start address of the allocated region.
 *
 * Return values:
 * @retval ferr_ok               The region has been allocated.
 * @retval ferr_invalid_argument One or more of the following: 1) @p byte_count was invalid (e.g. `SIZE_MAX`), 2) @p out_allocated_start was NULL.
 * @retval ferr_temporary_outage There was not enough memory available to satisfy the request.
 */
FERRO_WUR ferr_t fmempool_allocate(size_t byte_count, size_t* out_allocated_byte_count, void** out_allocated_start);

/**
 * A more advanced version of fmempool_allocate(), allowing for choice of alignment and physical contiguity, among other options.
 *
 * In addition to the parameters and return values of fmempool_allocate():
 *
 * @param alignment_power          A power of two for the alignment that the allocated region should have.
 *                                 For example, for 8-byte alignment, this should be 3 because 2^3 = 8.
 *                                 A value of 0 is 2^0 = 1, which is normal, unaligned memory.
 * @param boundary_alignment_power A power of two for the alignment of the boundary that the allocated region must not cross.
 *                                 The usage of this value is identical to that of @p alignment_power (e.g. a value of 3
 *                                 would result in a boundary alignment of 8 bytes).
 *                                 A value greater than 63 results in having no boundary alignment requirement.
 *                                 Note that `0` is a valid (yet absurd) value for this and does impose a 1-byte boundary alignment requirement.
 * @param flags                    An optional set of flags to modify how memory is allocated.
 *
 * @note Alignment and boundary alignment are not the same thing: alignment ensures that the region starts on a memory address
 *       with the given alignment, whereas boundary alignment ensures that the region does not cross a memory address with the given alignment.
 *       For example, allocating 48 bytes with a 16-byte alignment and no boundary alignment may result in the memory address 0xe0.
 *       However, allocating the same region with a 64-byte boundary alignment could not result in a region starting at 0xe0 because
 *       this region would cross a 64-byte boundary at 0x100. Additionally, allocating 48 bytes with no alignment and a 64-byte boundary alignment
 *       could result in a region starting at 0xc4, which is only 4-byte aligned but does not cross a 64-byte boundary.
 *
 * @retval ferr_invalid_argument In addition to possible reasons for ferr_invalid_argument from fmempool_allocate(), one or more of the following: 1) @p alignment_power was an invalid power (e.g. larger than `63`)
 */
FERRO_WUR ferr_t fmempool_allocate_advanced(size_t byte_count, uint8_t alignment_power, uint8_t boundary_alignment_power, fmempool_flags_t flags, size_t* out_allocated_byte_count, void** out_allocated_start);

/**
 * Reallocates a region of kernel memory to a new size.
 *
 * @param                     old_address The start address of the region to resize. It is valid to pass `NULL` for this, in which case the function behaves exactly like fmempool_allocate().
 * @param                  new_byte_count The number of bytes to allocate in the new memory block. The actual number of bytes allocated may be greater than this value (but never less). `0` is a valid value for this parameter.
 * @param[out] out_reallocated_byte_count Optional pointer in which to store the number of bytes actually allocated.
 * @param[out]      out_reallocated_start Pointer in which to store a pointer to the start address of the reallocated region.
 *
 * Return values:
 * @retval ferr_ok               The region has been reallocated, possibly to a different address.
 * @retval ferr_invalid_argument One or more of the following: 1) @p old_address was invalid (e.g. not previously allocated), 2) @p new_byte_count was invalid (e.g. `SIZE_MAX`), 3) @p out_reallocated_start was NULL.
 * @retval ferr_temporary_outage There was not enough memory available to satisfy the request. The existing memory remains untouched.
 */
FERRO_WUR ferr_t fmempool_reallocate(void* old_address, size_t new_byte_count, size_t* out_reallocated_byte_count, void** out_reallocated_start);

/**
 * A more advanced version of fmempool_reallocate(), allowing for choice of alignment and physical contiguity, among other options.
 *
 * In addition to the parameters and return values of fmempool_reallocate():
 *
 * @param alignment_power          See fmempool_allocate_advanced().
 * @param boundary_alignment_power See fmempool_allocate_advanced().
 * @param flags                    See fmempool_allocate_advanced().
 *
 * @retval ferr_invalid_argument In addition to possible reasons for ferr_invalid_argument from fmempool_reallocate(), one or more of the following: 1) @p alignment_power was an invalid power (e.g. larger than `63`)
 */
FERRO_WUR ferr_t fmempool_reallocate_advanced(void* old_address, size_t new_byte_count, uint8_t alignment_power, uint8_t boundary_alignment_power, fmempool_flags_t flags, size_t* out_reallocated_byte_count, void** out_reallocated_start);

/**
 * Frees a region of kernel memory previously allocated with fmempool_allocate().
 *
 * @param address The start address of the region to free.
 *
 * Return values:
 * @retval ferr_ok               The region has been freed.
 * @retval ferr_invalid_argument @p address was invalid (e.g. `NULL` or not previously allocated).
 */
FERRO_WUR ferr_t fmempool_free(void* address);

FERRO_DECLARATIONS_END;

#endif // _FERRO_CORE_MEMPOOL_H_
