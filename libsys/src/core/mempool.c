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

#ifndef LIBSYS_MEMCHECK
	#define LIBSYS_MEMCHECK 0
#endif

#if LIBSYS_MEMCHECK
	#include "./memcheck.c"
#else

#include <gen/libsyscall/syscall-wrappers.h>
#include <libsys/mempool.h>
#include <libsys/general.h>
#include <libsimple/libsimple.h>
#include <libsys/locks.h>
#include <libsys/pages.h>
#include <libsys/once.h>
#include <libsys/abort.h>
#include <libsys/config.h>
#include <libsys/console.h>
#include <libsys/threads.h>

sys_mutex_t mempool_global_lock = SYS_MUTEX_INIT;
static sys_mutex_t* handed_off_global_lock = NULL;

simple_mempool_instance_t mempool_main_instance;
static simple_mempool_instance_t physically_contiguous_instance;
static simple_mempool_instance_t* handed_off_main_instance = NULL;

static sys_once_t sys_mempool_init_token = SYS_ONCE_INITIALIZER;

void sys_mempool_handoff(sys_mutex_t* lock, simple_mempool_instance_t* instance) {
	handed_off_global_lock = lock;
	handed_off_main_instance = instance;
};

FERRO_ALWAYS_INLINE uintptr_t region_boundary(uintptr_t start, size_t length, uint8_t boundary_alignment_power) {
	if (boundary_alignment_power > 63) {
		return 0;
	}
	uintptr_t boundary_alignment_mask = (1ull << boundary_alignment_power) - 1;
	uintptr_t next_boundary = (start & ~boundary_alignment_mask) + (1ull << boundary_alignment_power);
	return (next_boundary > start && next_boundary < start + length) ? next_boundary : 0;
};

//
// main
//

static ferr_t sys_mempool_main_allocator_allocate(void* context, size_t page_count, uint8_t alignment_power, uint8_t boundary_alignment_power, void** out_allocated_start) {
	return sys_page_allocate_advanced(page_count, alignment_power, 0, out_allocated_start);
};

static ferr_t sys_mempool_main_allocator_free(void* context, size_t page_count, void* allocated_start) {
	return sys_page_free(allocated_start);
};

static ferr_t sys_mempool_main_allocator_allocate_header(void* context, size_t page_count, void** out_allocated_start) {
	return sys_page_allocate(page_count, 0, out_allocated_start);
};

static ferr_t sys_mempool_main_allocator_free_header(void* context, size_t page_count, void* allocated_start) {
	return sys_mempool_main_allocator_free(context, page_count, allocated_start);
};

//
// physically contiguous
//

static ferr_t sys_mempool_physically_contiguous_allocator_allocate(void* context, size_t page_count, uint8_t alignment_power, uint8_t boundary_alignment_power, void** out_allocated_start) {
	return sys_page_allocate_advanced(page_count, alignment_power, sys_page_flag_prebound | sys_page_flag_contiguous | sys_page_flag_uncacheable, out_allocated_start);
};

static ferr_t sys_mempool_physically_contiguous_allocator_free(void* context, size_t page_count, void* allocated_start) {
	return sys_page_free(allocated_start);
};

static ferr_t sys_mempool_physically_contiguous_allocator_allocate_header(void* context, size_t page_count, void** out_allocated_start) {
	return sys_page_allocate(page_count, 0, out_allocated_start);
};

static ferr_t sys_mempool_physically_contiguous_allocator_free_header(void* context, size_t page_count, void* allocated_start) {
	return sys_mempool_main_allocator_free(context, page_count, allocated_start);
};

static bool sys_mempool_physically_contiguous_is_aligned(void* context, void* address, size_t byte_count, uint8_t alignment_power, uint8_t boundary_alignment_power) {
	// if this is a physically contiguous region, also check that the physical region meets the boundary requirements
	// TODO: double-check whether we actually need to perform this check. i'm adding it just to be safe.
	uint64_t phys_start = 0;
	if (sys_page_translate(address, &phys_start) == ferr_ok) {
		return region_boundary(phys_start, byte_count, boundary_alignment_power) == 0;
	} else {
		return true;
	}
};

//
// common
//

static LIBSYS_NO_RETURN void sys_mempool_panic(const char* message, ...) {
	va_list args;
	va_start(args, message);
	sys_console_log_fv(message, args);
	va_end(args);
	sys_abort();
};

static const simple_mempool_allocator_t main_allocator = {
	.allocate = sys_mempool_main_allocator_allocate,
	.free = sys_mempool_main_allocator_free,
	.allocate_header = sys_mempool_main_allocator_allocate_header,
	.free_header = sys_mempool_main_allocator_free_header,
	.is_aligned = NULL,
	.panic = sys_mempool_panic,
};

static const simple_mempool_allocator_t physically_contiguous_allocator = {
	.allocate = sys_mempool_physically_contiguous_allocator_allocate,
	.free = sys_mempool_physically_contiguous_allocator_free,
	.allocate_header = sys_mempool_physically_contiguous_allocator_allocate_header,
	.free_header = sys_mempool_physically_contiguous_allocator_free_header,
	.is_aligned = sys_mempool_physically_contiguous_is_aligned,
	.panic = sys_mempool_panic,
};

static simple_mempool_instance_options_t options = {
	.page_size = /* filled in at runtime */ 0,
	.max_order = 32,
	.min_leaf_size = 16,
	.min_leaf_alignment = 4,
	.max_kept_region_count = 3,
	.optimal_min_region_order = 8, // this corresponds to a minimum region size of 4096 bytes with the current leaf size (16)
};

static void sys_mempool_do_init(void* context) {
	options.page_size = sys_config_read_page_size();
	sys_abort_status(simple_mempool_instance_init(&mempool_main_instance, NULL, &main_allocator, &options));
	sys_abort_status(simple_mempool_instance_init(&physically_contiguous_instance, NULL, &physically_contiguous_allocator, &options));
};

static void sys_mempool_ensure_init(void) {
	sys_once(&sys_mempool_init_token, sys_mempool_do_init, NULL, sys_once_flag_sigsafe);
};

LIBSYS_ALWAYS_INLINE void sys_mempool_lock(void) {
	sys_mutex_lock_sigsafe(&mempool_global_lock);
};

LIBSYS_ALWAYS_INLINE void sys_mempool_unlock(void) {
	sys_mutex_unlock_sigsafe(&mempool_global_lock);
};

ferr_t sys_mempool_allocate(size_t byte_count, size_t* out_allocated_byte_count, void** out_address) {
	return sys_mempool_allocate_advanced(byte_count, 0, UINT8_MAX, 0, out_allocated_byte_count, out_address);
};

ferr_t sys_mempool_reallocate(void* old_address, size_t new_byte_count, size_t* out_reallocated_byte_count, void** out_reallocated_start) {
	return sys_mempool_reallocate_advanced(old_address, new_byte_count, 0, UINT8_MAX, 0, out_reallocated_byte_count, out_reallocated_start);
};

ferr_t sys_mempool_free(void* address) {
	ferr_t status = ferr_ok;

	if (address == NULL) {
		return ferr_invalid_argument;
	} else if (address == LIBSIMPLE_MEMPOOL_NO_BYTES_POINTER) {
		return ferr_ok;
	}

	sys_mempool_ensure_init();

	sys_mempool_lock();
	status = simple_mempool_free(&mempool_main_instance, address);
	if (status != ferr_ok) {
		// now try the physically contiguous instance
		status = simple_mempool_free(&physically_contiguous_instance, address);
	}
	sys_mempool_unlock();

	// try the handed-off instance
	//
	// this allows us to free memory allocated in the dynamic linker;
	// this is necessary to e.g. release and destroy objects created in the dynamic linker.
	if (status != ferr_ok && handed_off_global_lock) {
		sys_mutex_lock(handed_off_global_lock);
		status = simple_mempool_free(handed_off_main_instance, address);
		sys_mutex_unlock(handed_off_global_lock);
	}

	return status;
};

static void find_target_instance(sys_mempool_flags_t flags, simple_mempool_instance_t** out_instance) {
	if (flags & sys_mempool_flag_physically_contiguous) {
		*out_instance = &physically_contiguous_instance;
	} else {
		*out_instance = &mempool_main_instance;
	}
};

ferr_t sys_mempool_allocate_advanced(size_t byte_count, uint8_t alignment_power, uint8_t boundary_alignment_power, sys_mempool_flags_t flags, size_t* out_allocated_byte_count, void** out_allocated_start) {
	simple_mempool_instance_t* instance = NULL;
	ferr_t status = ferr_ok;

	sys_mempool_ensure_init();

	find_target_instance(flags, &instance);

	sys_mempool_lock();
	status = simple_mempool_allocate(instance, byte_count, alignment_power, boundary_alignment_power, out_allocated_byte_count, out_allocated_start);
	sys_mempool_unlock();

out:
	return status;
};

static bool find_source_instance(void* address, simple_mempool_instance_t** out_instance) {
	if (simple_mempool_belongs_to_instance(&mempool_main_instance, address)) {
		*out_instance = &mempool_main_instance;
		return true;
	} else if (simple_mempool_belongs_to_instance(&physically_contiguous_instance, address)) {
		*out_instance = &physically_contiguous_instance;
		return true;
	}
	return false;
};

ferr_t sys_mempool_reallocate_advanced(void* old_address, size_t new_byte_count, uint8_t alignment_power, uint8_t boundary_alignment_power, sys_mempool_flags_t flags, size_t* out_reallocated_byte_count, void** out_reallocated_start) {
	ferr_t status = ferr_ok;
	simple_mempool_instance_t* source_instance = NULL;
	simple_mempool_instance_t* target_instance = NULL;

	if (old_address == LIBSIMPLE_MEMPOOL_NO_BYTES_POINTER || old_address == NULL) {
		return sys_mempool_allocate_advanced(new_byte_count, alignment_power, boundary_alignment_power, flags, out_reallocated_byte_count, out_reallocated_start);
	} else if (new_byte_count == 0) {
		status = sys_mempool_free(old_address);
		if (status != ferr_ok) {
			return status;
		}

		if (out_reallocated_byte_count) {
			*out_reallocated_byte_count = 0;
		}

		*out_reallocated_start = LIBSIMPLE_MEMPOOL_NO_BYTES_POINTER;

		return status;
	}

	sys_mempool_ensure_init();

	find_target_instance(flags, &target_instance);

	sys_mempool_lock();

	if (!find_source_instance(old_address, &source_instance)) {
		// not allocated
		status = ferr_invalid_argument;
		goto out;
	}

	if (source_instance != target_instance) {
		// slow route: allocate new region and copy
		// alright, looks like we gotta allocate a new region
		void* new_address = NULL;
		size_t new_allocated_byte_count = 0;
		size_t old_byte_count = 0;

		// this cannot fail
		if (!simple_mempool_get_allocated_byte_count(source_instance, old_address, &old_byte_count)) {
			sys_mempool_panic("Failed to get allocated byte count for old address");
		}

		// drop the mempool lock
		sys_mempool_unlock();

		// allocate the new region
		status = sys_mempool_allocate_advanced(new_byte_count, alignment_power, boundary_alignment_power, flags, &new_allocated_byte_count, &new_address);

		if (status != ferr_ok) {
			return status;
		}

		// next, copy the old data
		simple_memcpy(new_address, old_address, old_byte_count);

		// finally, free the old region
		if (sys_mempool_free(old_address) != ferr_ok) {
			// this literally can't fail
			sys_mempool_panic("Failed to free old address during simple_mempool_reallocate");
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
	status = simple_mempool_reallocate(source_instance, old_address, new_byte_count, alignment_power, boundary_alignment_power, out_reallocated_byte_count, out_reallocated_start);

out:
	sys_mempool_unlock();
out_unlocked:
	return status;
};

#endif // !LIBSYS_MEMCHECK
