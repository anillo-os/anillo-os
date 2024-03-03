/*
 * This file is part of Anillo OS
 * Copyright (C) 2024 Anillo OS Developers
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

#include <libsys/mempool.h>

#include <stdlib.h>
#include <errno.h>

ferr_t sys_mempool_allocate(size_t byte_count, size_t* out_allocated_byte_count, void** out_address) {
	*out_address = malloc(byte_count);
	if (out_allocated_byte_count) {
		*out_allocated_byte_count = byte_count;
	}
	return (*out_address) ? ferr_ok : ferr_temporary_outage;
};

ferr_t sys_mempool_allocate_advanced(size_t byte_count, uint8_t alignment_power, uint8_t boundary_alignment_power, sys_mempool_flags_t flags, size_t* out_allocated_byte_count, void** out_allocated_start) {
	if (flags != 0 || boundary_alignment_power != UINT8_MAX) {
		return ferr_unsupported;
	}

	if (out_allocated_byte_count) {
		*out_allocated_byte_count = byte_count;
	}

	switch (posix_memalign(out_allocated_start, ((size_t)1) << ((size_t)alignment_power), byte_count)) {
		case 0:
			return ferr_ok;
		case EINVAL:
			return ferr_invalid_argument;
		case ENOMEM:
			return ferr_temporary_outage;
		default:
			return ferr_unknown;
	}
};

ferr_t sys_mempool_reallocate(void* old_address, size_t new_byte_count, size_t* out_reallocated_byte_count, void** out_reallocated_start) {
	void* result = realloc(old_address, new_byte_count);
	if (!result) {
		return ferr_temporary_outage;
	}

	*out_reallocated_start = result;
	if (out_reallocated_byte_count) {
		*out_reallocated_byte_count = new_byte_count;
	}
	return ferr_ok;
};

ferr_t sys_mempool_reallocate_advanced(void* old_address, size_t new_byte_count, uint8_t alignment_power, uint8_t boundary_alignment_power, sys_mempool_flags_t flags, size_t* out_reallocated_byte_count, void** out_reallocated_start) {
	return ferr_unsupported;
};

ferr_t sys_mempool_free(void* address) {
	free(address);
	return ferr_ok;
};
