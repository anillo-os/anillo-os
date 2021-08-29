/*
 * This file is part of Anillo OS
 * Copyright (C) 2020 Anillo OS Developers
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

#ifndef _FERRO_CORE_MEMPOOL_H_
#define _FERRO_CORE_MEMPOOL_H_

#include <stddef.h>

#include <ferro/base.h>
#include <ferro/error.h>

FERRO_DECLARATIONS_BEGIN;

/**
 * Allocates a region of kernel memory of the given size.
 *
 * @param byte_count               The number of bytes to allocate in the memory block. The actual number of bytes allocated may be greater than this value (but never less). `0` is a valid value for this parameter.
 * @param out_allocated_byte_count Optional pointer in which to store the number of bytes actually allocated.
 * @param out_allocated_start      Pointer in which to store a pointer to the start address of the allocated region.
 *
 * Return values:
 * @retval ferr_ok               The region has been allocated.
 * @retval ferr_invalid_argument One or more of the following: 1) `byte_count` was invalid (e.g. SIZE_MAX), 2) `out_allocated_start` was NULL.
 * @retval ferr_temporary_outage There was not enough memory available to satisfy the request.
 */
FERRO_WUR ferr_t fmempool_allocate(size_t byte_count, size_t* out_allocated_byte_count, void** out_allocated_start);

/**
 * Reallocates a region of kernel memory to a new size.
 *
 * @param old_address                The start address of the region to resize. It is valid to pass `NULL` for this, in which case the function behaves exactly like `fmempool_allocate`.
 * @param new_byte_count             The number of bytes to allocate in the new memory block. The actual number of bytes allocated may be greater than this value (but never less). `0` is a valid value for this parameter.
 * @param out_reallocated_byte_count Optional pointer in which to store the number of bytes actually allocated.
 * @param out_reallocated_start      Pointer in which to store a pointer to the start address of the reallocated region.
 *
 * Return values:
 * @retval ferr_ok               The region has been reallocated, possibly to a different address.
 * @retval ferr_invalid_argument One or more of the following: 1) `old_address` was invalid (e.g. not previously allocated), 2) `new_byte_count` was invalid (e.g. SIZE_MAX), 3) `out_reallocated_start` was NULL.
 * @retval ferr_temporary_outage There was not enough memory available to satisfy the request. The existing memory remains untouched.
 */
FERRO_WUR ferr_t fmempool_reallocate(void* old_address, size_t new_byte_count, size_t* out_reallocated_byte_count, void** out_reallocated_start);

/**
 * Frees a region of kernel memory previously allocated with @link{fmempool_allocate}.
 *
 * @param address The start address of the region to free.
 *
 * Return values:
 * @retval ferr_ok               The region has been freed.
 * @retval ferr_invalid_argument `address` was invalid (e.g. `NULL` or not previously allocated).
 */
FERRO_WUR ferr_t fmempool_free(void* address);

FERRO_DECLARATIONS_END;

#endif // _FERRO_CORE_MEMPOOL_H_
