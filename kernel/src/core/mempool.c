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
 * Kernel memory pool management (e.g. de/allocation).
 */

#include <stdint.h>
#include <stdbool.h>

#include <ferro/core/mempool.h>
#include <ferro/bits.h>
#include <ferro/core/paging.h>
#include <ferro/core/panic.h>
#include <ferro/core/locks.h>
#include <libsimple/libsimple.h>

// for fpage_prefault_stack
#include <ferro/core/paging.private.h>

// 4-8KiB should be enough
#define FMEMPOOL_PREFAULT_PAGE_COUNT 2

#ifndef FMEMPOOL_DEBUG_ALWAYS_PREBIND
	#define FMEMPOOL_DEBUG_ALWAYS_PREBIND 0
#endif

// maximum order of a single allocation
#define MAX_ORDER 32

// size of a single leaf in bytes, including the header
#define LEAF_SIZE 16
#define LEAF_MIN_ALIGNMENT 4

// how many regions to keep when freeing completely unused regions
#define KEPT_REGION_COUNT 3

//
// main
//

static ferr_t fmempool_main_allocate(void* context, size_t page_count, uint8_t alignment_power, uint8_t boundary_alignment_power, void** out_allocated_start) {
	return fpage_space_allocate_aligned(fpage_space_kernel(), page_count, alignment_power, out_allocated_start, 0);
};

static ferr_t fmempool_main_free(void* context, size_t page_count, void* allocated_start) {
	return fpage_space_free(fpage_space_kernel(), allocated_start, page_count);
};

static ferr_t fmempool_main_allocate_header(void* context, size_t page_count, void** out_allocated_start) {
	return fpage_space_allocate(fpage_space_kernel(), page_count, out_allocated_start, 0);
};

static ferr_t fmempool_main_free_header(void* context, size_t page_count, void* allocated_start) {
	return fmempool_main_free(context, page_count, allocated_start);
};

//
// physically contiguous
//

static ferr_t fmempool_physically_contiguous_allocate(void* context, size_t page_count, uint8_t alignment_power, uint8_t boundary_alignment_power, void** out_allocated_start) {
	void* physical_region_start = NULL;
	ferr_t status = ferr_ok;

	// TODO: in some cases, callers are okay with having aligned physical memory with unaligned virtual memory
	//       or vice versa. for now, we always allocate both the physical and virtual memory with the desired alignment,
	//       but we could add an option to allow either one to be unaligned for memory efficiency purposes.

	status = fpage_allocate_physical_aligned(page_count, alignment_power, NULL, &physical_region_start, 0);
	if (status != ferr_ok) {
		goto out;
	}

	status = fpage_space_map_aligned(fpage_space_kernel(), physical_region_start, page_count, alignment_power, out_allocated_start, 0);
	if (status != ferr_ok) {
		goto out;
	}

out:
	if (status != ferr_ok) {
		if (physical_region_start) {
			FERRO_WUR_IGNORE(fpage_free_physical(physical_region_start, page_count));
		}
	}
	return status;
};

static ferr_t fmempool_physically_contiguous_free(void* context, size_t page_count, void* allocated_start) {
	void* physical_start = (void*)fpage_space_virtual_to_physical(fpage_space_kernel(), (uintptr_t)allocated_start);
	fassert(physical_start != (void*)UINTPTR_MAX);
	fpanic_status(fpage_space_unmap(fpage_space_kernel(), allocated_start, page_count));
	fpanic_status(fpage_free_physical(physical_start, page_count));
	return ferr_ok;
};

static ferr_t fmempool_physically_contiguous_allocate_header(void* context, size_t page_count, void** out_allocated_start) {
	return fmempool_main_allocate_header(context, page_count, out_allocated_start);
};

static ferr_t fmempool_physically_contiguous_free_header(void* context, size_t page_count, void* allocated_start) {
	return fmempool_main_free_header(context, page_count, allocated_start);
};

static bool fmempool_physically_contiguous_is_aligned(void* context, void* address, size_t byte_count, uint8_t alignment_power, uint8_t boundary_alignment_power) {
	// if this is a physically contiguous region, also check that the physical region meets the boundary requirements
	// TODO: double-check whether we actually need to perform this check. i'm adding it just to be safe.
	uintptr_t phys_start = fpage_space_virtual_to_physical(fpage_space_kernel(), (uintptr_t)address);
	return fpage_region_boundary((uintptr_t)phys_start, byte_count, boundary_alignment_power) == 0;
};

//
// prebound
//

static ferr_t fmempool_prebound_allocate(void* context, size_t page_count, uint8_t alignment_power, uint8_t boundary_alignment_power, void** out_allocated_start) {
	return fpage_space_allocate_aligned(fpage_space_kernel(), page_count, alignment_power, out_allocated_start, fpage_flag_prebound);
};

static ferr_t fmempool_prebound_free(void* context, size_t page_count, void* allocated_start) {
	return fmempool_main_free(context, page_count, allocated_start);
};

static ferr_t fmempool_prebound_allocate_header(void* context, size_t page_count, void** out_allocated_start) {
	return fpage_space_allocate(fpage_space_kernel(), page_count, out_allocated_start, fpage_flag_prebound);
};

static ferr_t fmempool_prebound_free_header(void* context, size_t page_count, void* allocated_start) {
	return fmempool_main_free_header(context, page_count, allocated_start);
};

//
// public api
//

static simple_mempool_instance_t main_instance;
static simple_mempool_instance_t physically_contiguous_instance;
static simple_mempool_instance_t prebound_instance;

// these locks protect each of their respective instances
static flock_spin_intsafe_t main_instance_lock = FLOCK_SPIN_INTSAFE_INIT;
static flock_spin_intsafe_t physically_contiguous_instance_lock = FLOCK_SPIN_INTSAFE_INIT;
static flock_spin_intsafe_t prebound_instance_lock = FLOCK_SPIN_INTSAFE_INIT;

static const simple_mempool_allocator_t main_allocator = {
	.allocate = fmempool_main_allocate,
	.free = fmempool_main_free,
	.allocate_header = fmempool_main_allocate_header,
	.free_header = fmempool_main_free_header,
	.is_aligned = NULL,
	.panic = fpanic,
};

static const simple_mempool_allocator_t physically_contiguous_allocator = {
	.allocate = fmempool_physically_contiguous_allocate,
	.free = fmempool_physically_contiguous_free,
	.allocate_header = fmempool_physically_contiguous_allocate_header,
	.free_header = fmempool_physically_contiguous_free_header,
	.is_aligned = fmempool_physically_contiguous_is_aligned,
	.panic = fpanic,
};

static const simple_mempool_allocator_t prebound_allocator = {
	.allocate = fmempool_prebound_allocate,
	.free = fmempool_prebound_free,
	.allocate_header = fmempool_prebound_allocate_header,
	.free_header = fmempool_prebound_free_header,
	.is_aligned = NULL,
	.panic = fpanic,
};

static const simple_mempool_instance_options_t options = {
	.page_size = FPAGE_PAGE_SIZE,
	.max_order = MAX_ORDER,
	.min_leaf_size = LEAF_SIZE,
	.min_leaf_alignment = LEAF_MIN_ALIGNMENT,
	.max_kept_region_count = KEPT_REGION_COUNT,
};

void fmempool_init(void) {
	fpanic_status(simple_mempool_instance_init(&main_instance, NULL, &main_allocator, &options));
	fpanic_status(simple_mempool_instance_init(&physically_contiguous_instance, NULL, &physically_contiguous_allocator, &options));
	fpanic_status(simple_mempool_instance_init(&prebound_instance, NULL, &prebound_allocator, &options));
};

static void find_target_instance(fmempool_flags_t flags, simple_mempool_instance_t** out_instance, flock_spin_intsafe_t** out_lock) {
	if (flags & fmempool_flag_physically_contiguous) {
		*out_instance = &physically_contiguous_instance;
		*out_lock = &physically_contiguous_instance_lock;
	} else if (flags & fmempool_flag_prebound) {
		*out_instance = &prebound_instance;
		*out_lock = &prebound_instance_lock;
	} else {
		*out_instance = &main_instance;
		*out_lock = &main_instance_lock;
	}
};

ferr_t fmempool_allocate_advanced(size_t byte_count, uint8_t alignment_power, uint8_t boundary_alignment_power, fmempool_flags_t flags, size_t* out_allocated_byte_count, void** out_allocated_start) {
	simple_mempool_instance_t* instance = NULL;
	flock_spin_intsafe_t* lock = NULL;
	ferr_t status = ferr_ok;

	find_target_instance(flags, &instance, &lock);

	fpage_prefault_stack(FMEMPOOL_PREFAULT_PAGE_COUNT);

	flock_spin_intsafe_lock(lock);
	status = simple_mempool_allocate(instance, byte_count, alignment_power, boundary_alignment_power, out_allocated_byte_count, out_allocated_start);
	flock_spin_intsafe_unlock(lock);

	return status;
};

ferr_t fmempool_allocate(size_t byte_count, size_t* out_allocated_byte_count, void** out_allocated_start) {
	return fmempool_allocate_advanced(byte_count, 0, UINT8_MAX, 0, out_allocated_byte_count, out_allocated_start);
};

static bool find_instance(void* address, simple_mempool_instance_t** out_instance, flock_spin_intsafe_t** out_lock) {
	flock_spin_intsafe_lock(&main_instance_lock);
	if (simple_mempool_belongs_to_instance(&main_instance, address)) {
		*out_instance = &main_instance;
		*out_lock = &main_instance_lock;
		return true;
	}
	flock_spin_intsafe_unlock(&main_instance_lock);

	flock_spin_intsafe_lock(&prebound_instance_lock);
	if (simple_mempool_belongs_to_instance(&prebound_instance, address)) {
		*out_instance = &prebound_instance;
		*out_lock = &prebound_instance_lock;
		return true;
	}
	flock_spin_intsafe_unlock(&prebound_instance_lock);

	flock_spin_intsafe_lock(&physically_contiguous_instance_lock);
	if (simple_mempool_belongs_to_instance(&physically_contiguous_instance, address)) {
		*out_instance = &physically_contiguous_instance;
		*out_lock = &physically_contiguous_instance_lock;
		return true;
	}
	flock_spin_intsafe_unlock(&physically_contiguous_instance_lock);

	return false;
};

ferr_t fmempool_reallocate_advanced(void* old_address, size_t new_byte_count, uint8_t alignment_power, uint8_t boundary_alignment_power, fmempool_flags_t flags, size_t* out_reallocated_byte_count, void** out_reallocated_start) {
	ferr_t status = ferr_ok;
	simple_mempool_instance_t* old_instance = NULL;
	flock_spin_intsafe_t* old_lock = NULL;

	if (old_address == LIBSIMPLE_MEMPOOL_NO_BYTES_POINTER || old_address == NULL) {
		return fmempool_allocate_advanced(new_byte_count, alignment_power, boundary_alignment_power, flags, out_reallocated_byte_count, out_reallocated_start);
	} else if (new_byte_count == 0) {
		status = fmempool_free(old_address);
		if (status != ferr_ok) {
			return status;
		}

		if (out_reallocated_byte_count) {
			*out_reallocated_byte_count = 0;
		}

		*out_reallocated_start = LIBSIMPLE_MEMPOOL_NO_BYTES_POINTER;

		return status;
	}

	fpage_prefault_stack(FMEMPOOL_PREFAULT_PAGE_COUNT);

	if (!find_instance(old_address, &old_instance, &old_lock)) {
		// not allocated
		return ferr_invalid_argument;
	}

	// the old instance's lock is held here

	// if we're switching between instances, we have to go the slow route
	if (
		((flags & fmempool_flag_physically_contiguous) != 0) != (old_instance == &physically_contiguous_instance) ||
		((flags & fmempool_flag_prebound) != 0) != (old_instance == &prebound_instance)
	) {
		// alright, looks like we gotta allocate a new region
		void* new_address = NULL;
		size_t new_allocated_byte_count = 0;
		size_t old_byte_count = 0;

		// this cannot fail
		if (!simple_mempool_get_allocated_byte_count(old_instance, old_address, &old_byte_count)) {
			fpanic("Failed to get allocated byte count for old address");
		}

		// drop the old lock
		flock_spin_intsafe_unlock(old_lock);

		// allocate the new region
		status = fmempool_allocate_advanced(new_byte_count, alignment_power, boundary_alignment_power, flags, &new_allocated_byte_count, &new_address);

		if (status != ferr_ok) {
			return status;
		}

		// next, copy the old data
		simple_memcpy(new_address, old_address, old_byte_count);

		// finally, free the old region
		if (fmempool_free(old_address) != ferr_ok) {
			// this literally can't fail
			fpanic("Failed to free old address during simple_mempool_reallocate");
		}

		if (status == ferr_ok) {
			*out_reallocated_start = new_address;
			if (out_reallocated_byte_count) {
				*out_reallocated_byte_count = new_allocated_byte_count;
			}
		}
		return status;
	}

	// otherwise, defer to simple_mempool
	status = simple_mempool_reallocate(old_instance, old_address, new_byte_count, alignment_power, boundary_alignment_power, out_reallocated_byte_count, out_reallocated_start);
	flock_spin_intsafe_unlock(old_lock);

	return status;
};

ferr_t fmempool_reallocate(void* old_address, size_t new_byte_count, size_t* out_reallocated_byte_count, void** out_reallocated_start) {
	return fmempool_reallocate_advanced(old_address, new_byte_count, 0, UINT8_MAX, 0, out_reallocated_byte_count, out_reallocated_start);
};

ferr_t fmempool_free(void* address) {
	if (address == NULL) {
		return ferr_invalid_argument;
	} else if (address == LIBSIMPLE_MEMPOOL_NO_BYTES_POINTER) {
		return ferr_ok;
	}

	fpage_prefault_stack(FMEMPOOL_PREFAULT_PAGE_COUNT);

	flock_spin_intsafe_lock(&main_instance_lock);
	if (simple_mempool_free(&main_instance, address) == ferr_ok) {
		flock_spin_intsafe_unlock(&main_instance_lock);
		return ferr_ok;
	}
	flock_spin_intsafe_unlock(&main_instance_lock);

	flock_spin_intsafe_lock(&prebound_instance_lock);
	if (simple_mempool_free(&prebound_instance, address) == ferr_ok) {
	flock_spin_intsafe_unlock(&prebound_instance_lock);
		return ferr_ok;
	}
	flock_spin_intsafe_unlock(&prebound_instance_lock);

	flock_spin_intsafe_lock(&physically_contiguous_instance_lock);
	if (simple_mempool_free(&physically_contiguous_instance, address) == ferr_ok) {
	flock_spin_intsafe_unlock(&physically_contiguous_instance_lock);
		return ferr_ok;
	}
	flock_spin_intsafe_unlock(&physically_contiguous_instance_lock);

	return ferr_invalid_argument;
};
