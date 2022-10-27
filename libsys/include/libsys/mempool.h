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

#ifndef _LIBSYS_MEMPOOL_H_
#define _LIBSYS_MEMPOOL_H_

#include <stdint.h>
#include <stddef.h>

#include <libsys/base.h>
#include <ferro/error.h>

LIBSYS_DECLARATIONS_BEGIN;

FERRO_OPTIONS(uint64_t, sys_mempool_flags) {
	/**
	 * Allocated memory must be physically contiguous.
	 */
	sys_mempool_flag_physically_contiguous = 1 << 0,
};

/**
 * Allocates some memory in the memory pool (a.k.a. the heap).
 *
 * @param byte_count               The number of bytes to allocate.
 * @param out_allocated_byte_count An optional pointer in which to write the number of bytes actually allocated.
 *                                 The number of bytes allocated will always be greater than or equal to @p byte_count on success.
 *                                 This is useful for certain callers that wish to use all the available space allocated for them.
 * @param out_address              A pointer in which a pointer to the start of the allocated region will be written.
 *
 * @retval ferr_ok               The memory was successfully allocated and a pointer to the start of the allocated region has been written into @p out_address.
 * @retval ferr_temporary_outage There were insufficient resources to allocate the request number of bytes.
 * @retval ferr_invalid_argument @p out_address was `NULL`.
 * @retval ferr_forbidden        The calling process was not allowed to allocate memory.
 */
LIBSYS_WUR ferr_t sys_mempool_allocate(size_t byte_count, size_t* out_allocated_byte_count, void** out_address);

LIBSYS_WUR ferr_t sys_mempool_allocate_advanced(size_t byte_count, uint8_t alignment_power, uint8_t boundary_alignment_power, sys_mempool_flags_t flags, size_t* out_allocated_byte_count, void** out_allocated_start);

LIBSYS_WUR ferr_t sys_mempool_reallocate(void* old_address, size_t new_byte_count, size_t* out_reallocated_byte_count, void** out_reallocated_start);

LIBSYS_WUR ferr_t sys_mempool_reallocate_advanced(void* old_address, size_t new_byte_count, uint8_t alignment_power, uint8_t boundary_alignment_power, sys_mempool_flags_t flags, size_t* out_reallocated_byte_count, void** out_reallocated_start);

LIBSYS_WUR ferr_t sys_mempool_free(void* address);

LIBSYS_DECLARATIONS_END;

#endif // _LIBSYS_MEMPOOL_H_
