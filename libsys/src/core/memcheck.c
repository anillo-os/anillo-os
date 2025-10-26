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

#include <libsys/mempool.private.h>
#include <libsys/pages.h>
#include <libsys/config.h>
#include <libsimple/libsimple.h>
#include <libsys/console.h>
#include <libsys/abort.h>

LIBSYS_STRUCT(sys_mempool_allocation_info) {
	uint64_t byte_count;
};

sys_mutex_t mempool_global_lock = SYS_MUTEX_INIT;
simple_mempool_instance_t mempool_main_instance = {0};

static sys_mutex_t* handed_off_global_lock = NULL;
static simple_mempool_instance_t* handed_off_main_instance = NULL;

static LIBSYS_NO_RETURN void sys_mempool_panic(const char* message, ...) {
	va_list args;
	va_start(args, message);
	sys_console_log_fv(message, args);
	va_end(args);
	sys_abort();
};

void sys_mempool_handoff(sys_mutex_t* lock, simple_mempool_instance_t* instance) {
	handed_off_global_lock = lock;
	handed_off_main_instance = instance;
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

static void sys_mempool_write_info(const sys_mempool_allocation_info_t* alloc_info, void* out_page_start) {
	size_t page_count = sys_page_round_up_count(alloc_info->byte_count);
	uint64_t page_size = sys_config_read_page_size();

	simple_memcpy((void*)((uintptr_t)out_page_start                                                       ), alloc_info, sizeof(*alloc_info));
	simple_memcpy((void*)((uintptr_t)out_page_start + (page_size                   ) - sizeof(*alloc_info)), alloc_info, sizeof(*alloc_info));
	simple_memcpy((void*)((uintptr_t)out_page_start + (page_size * (page_count + 1))                      ), alloc_info, sizeof(*alloc_info));
	simple_memcpy((void*)((uintptr_t)out_page_start + (page_size * (page_count + 2)) - sizeof(*alloc_info)), alloc_info, sizeof(*alloc_info));
};

static ferr_t sys_mempool_read_info(void* page_start, sys_mempool_allocation_info_t* out_alloc_info) {
	sys_mempool_allocation_info_t tmp;
	uint64_t page_size = sys_config_read_page_size();
	size_t page_count = 0;

	simple_memcpy(&tmp, page_start, sizeof(tmp));
	if (simple_memcmp(&tmp, (void*)((uintptr_t)page_start + page_size - sizeof(tmp)), sizeof(tmp)) != 0) {
		return ferr_aborted;
	}

	page_count = sys_page_round_up_count(tmp.byte_count);

	if (simple_memcmp(&tmp, (void*)((uintptr_t)page_start + (page_size * (page_count + 1))), sizeof(tmp)) != 0) {
		return ferr_aborted;
	}

	if (simple_memcmp(&tmp, (void*)((uintptr_t)page_start + (page_size * (page_count + 2)) - sizeof(tmp)), sizeof(tmp)) != 0) {
		return ferr_aborted;
	}

	simple_memcpy(out_alloc_info, &tmp, sizeof(tmp));
	return ferr_ok;
};

ferr_t sys_mempool_allocate_advanced(size_t byte_count, uint8_t alignment_power, uint8_t boundary_alignment_power, sys_mempool_flags_t flags, size_t* out_allocated_byte_count, void** out_allocated_start) {
	ferr_t status = ferr_ok;
	size_t page_count = sys_page_round_up_count(byte_count);
	void* page_start = NULL;
	uint64_t page_size = sys_config_read_page_size();
	sys_mempool_allocation_info_t alloc_info = {
		.byte_count = byte_count,
	};

	sys_mempool_lock();

	status = sys_page_allocate(page_count + 2, 0, &page_start);
	if (status != ferr_ok) {
		goto out;
	}

	if (out_allocated_byte_count) {
		*out_allocated_byte_count = byte_count;
	}

	sys_mempool_write_info(&alloc_info, page_start);

	*out_allocated_start = (void*)((uintptr_t)page_start + page_size);

out:
	sys_mempool_unlock();
	return status;
};

ferr_t sys_mempool_reallocate_advanced(void* old_address, size_t new_byte_count, uint8_t alignment_power, uint8_t boundary_alignment_power, sys_mempool_flags_t flags, size_t* out_reallocated_byte_count, void** out_reallocated_start) {
	ferr_t status = ferr_ok;
	void* new_address = NULL;
	size_t new_allocated_byte_count = 0;
	sys_mempool_allocation_info_t old_alloc_info;
	uint64_t page_size = sys_config_read_page_size();

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

	sys_mempool_lock();

	if (sys_mempool_read_info((void*)((uintptr_t)old_address - page_size), &old_alloc_info) != ferr_ok) {
		sys_mempool_panic("INVALID/CORRUPTED ALLOCATION INFO");
	}

	sys_mempool_unlock();

	// allocate the new region
	status = sys_mempool_allocate_advanced(new_byte_count, alignment_power, boundary_alignment_power, flags, &new_allocated_byte_count, &new_address);

	if (status != ferr_ok) {
		return status;
	}

	// next, copy the old data
	simple_memcpy(new_address, old_address, old_alloc_info.byte_count);

	// finally, free the old region
	if (sys_mempool_free(old_address) != ferr_ok) {
		// this literally can't fail
		sys_mempool_panic("Failed to free old address during simple_mempool_reallocate");
	}

out:
	if (status == ferr_ok) {
		*out_reallocated_start = new_address;
		if (out_reallocated_byte_count) {
			*out_reallocated_byte_count = new_allocated_byte_count;
		}
	}
	return status;
};

ferr_t sys_mempool_free(void* address) {
	ferr_t status = ferr_ok;
	sys_mempool_allocation_info_t alloc_info;
	uint64_t page_size = sys_config_read_page_size();

	// check if it comes from the handed-off instance
	//
	// this allows us to free memory allocated in the dynamic linker;
	// this is necessary to e.g. release and destroy objects created in the dynamic linker.
	if (handed_off_global_lock && handed_off_main_instance) {
		sys_mutex_lock(handed_off_global_lock);
		status = simple_mempool_belongs_to_instance(handed_off_main_instance, address);
		sys_mutex_unlock(handed_off_global_lock);
	}

	if (status != ferr_ok) {
		sys_mempool_lock();

		if (sys_mempool_read_info((void*)((uintptr_t)address - page_size), &alloc_info) != ferr_ok) {
			sys_mempool_panic("INVALID/CORRUPTED ALLOCATION INFO");
		}

		status = sys_page_free((void*)((uintptr_t)address - page_size));

		sys_mempool_unlock();
	}

out:
	return status;
};
