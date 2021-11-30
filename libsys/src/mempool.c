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

#include <libsys/mempool.h>
#include <libsys/general.h>
#include <libsimple/libsimple.h>
#include <libsys/locks.h>
#include <libsys/pages.h>

#define DLMALLOC_EXPORT static
#define USE_DL_PREFIX 1
#define ABORT sys_mempool_abort
#define MALLOC_FAILURE_ACTION
#define HAVE_MORECORE 0
#define HAVE_MMAP 1
#define HAVE_MREMAP 0
#define MMAP_CLEARS 0
#define USE_BUILTIN_FFS 1
#define malloc_getpagesize ((size_t)4096ULL)
#define USE_DEV_RANDOM 0
#define NO_MALLINFO 1
#define NO_MALLOC_STATS 1

#define LACKS_UNISTD_H    1
#define LACKS_FCNTL_H     1
#define LACKS_SYS_PARAM_H 1
#define LACKS_SYS_MMAN_H  1
#define LACKS_STRINGS_H   1
#define LACKS_STRING_H    1
#define LACKS_SYS_TYPES_H 1
#define LACKS_ERRNO_H     1
#define LACKS_STDLIB_H    1
#define LACKS_SCHED_H     1
#define LACKS_TIME_H      1

#define ffs __builtin_ffs

#define USE_LOCKS 0

#define MMAP(_size) sys_mempool_mmap(_size)
#define MUNMAP(_addr, _size) sys_mempool_munmap(_addr, _size)
#define DIRECT_MMAP(_size) MMAP(_size)

#define EINVAL ferr_invalid_argument
#define ENOMEM ferr_temporary_outage

#define memcpy simple_memcpy
#define memset simple_memset

#define USAGE_ERROR_ACTION(s, p) sys_mempool_usage_error = true;

LIBSYS_NO_RETURN static void sys_mempool_abort(void) {
	sys_exit(1);
};

// TODO: page allocation

#define sys_mempool_mmap_fail ((void*)SIZE_MAX)
#define PAGE_SIZE 4096ULL

FERRO_ALWAYS_INLINE uint64_t round_up_page(uint64_t number) {
	return (number + PAGE_SIZE - 1) & -PAGE_SIZE;
};

FERRO_ALWAYS_INLINE uint64_t round_up_to_page_count(uint64_t byte_count) {
	return round_up_page(byte_count) / PAGE_SIZE;
};

static void* sys_mempool_mmap(size_t byte_size) {
	void* addr = NULL;
	if (sys_page_allocate(round_up_to_page_count(byte_size), 0, &addr) != ferr_ok) {
		return sys_mempool_mmap_fail;
	}
	return addr;
};

static int sys_mempool_munmap(void* address, size_t byte_size) {
	if (sys_page_free(address) != ferr_ok) {
		return -1;
	}
	return 0;
};

// this global is protected by the mempool lock
static bool sys_mempool_usage_error = false;

#include "dlmalloc.c"

// TODO: the mempool lock should be a mutex (once we get those)
static sys_spinlock_t sys_mempool_global_lock = SYS_SPINLOCK_INIT;

LIBSYS_ALWAYS_INLINE void sys_mempool_lock(void) {
	return sys_spinlock_lock(&sys_mempool_global_lock);
};

LIBSYS_ALWAYS_INLINE void sys_mempool_unlock(void) {
	return sys_spinlock_unlock(&sys_mempool_global_lock);
};

ferr_t sys_mempool_allocate(size_t byte_count, size_t* out_allocated_byte_count, void** out_address) {
	void* address = NULL;
	ferr_t status = ferr_ok;

	if (!out_address) {
		status = ferr_invalid_argument;
		goto out_unlocked;
	}

	sys_mempool_lock();

	address = dlmalloc(byte_count);

	if (sys_mempool_usage_error) {
		sys_mempool_usage_error = false;
		status = ferr_invalid_argument;
		goto out;
	}

	if (!address) {
		status = ferr_temporary_outage;
		goto out;
	}

	if (out_allocated_byte_count) {
		*out_allocated_byte_count = dlmalloc_usable_size(address);
	}

	*out_address = address;

out:
	sys_mempool_unlock();
out_unlocked:
	return status;
};

ferr_t sys_mempool_reallocate(void* old_address, size_t new_byte_count, size_t* out_reallocated_byte_count, void** out_reallocated_start) {
	void* address = NULL;
	ferr_t status = ferr_ok;

	if (!out_reallocated_start) {
		status = ferr_invalid_argument;
		goto out_unlocked;
	}

	sys_mempool_lock();

	address = dlrealloc(old_address, new_byte_count);

	if (sys_mempool_usage_error) {
		sys_mempool_usage_error = false;
		status = ferr_invalid_argument;
		goto out;
	}

	if (!address) {
		status = ferr_temporary_outage;
		goto out;
	}

	if (out_reallocated_byte_count) {
		*out_reallocated_byte_count = dlmalloc_usable_size(address);
	}

	*out_reallocated_start = address;

out:
	sys_mempool_unlock();
out_unlocked:
	return status;
};

ferr_t sys_mempool_free(void* address) {
	ferr_t status = ferr_ok;

	sys_mempool_lock();

	dlfree(address);

	if (sys_mempool_usage_error) {
		sys_mempool_usage_error = false;
		status = ferr_invalid_argument;
		goto out;
	}

out:
	sys_mempool_unlock();
out_unlocked:
	return status;
};
