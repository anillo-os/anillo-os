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

#include <libsys/pages.private.h>
#include <gen/libsyscall/syscall-wrappers.h>

static void sys_shared_memory_destroy(sys_object_t* obj) {
	sys_shared_memory_object_t* shared_memory = (void*)obj;
	if (shared_memory->did != UINT64_MAX) {
		LIBSYS_WUR_IGNORE(libsyscall_wrapper_page_close_shared(shared_memory->did));
	}
	sys_object_destroy(obj);
};

static const sys_object_class_t shared_memory_class = {
	LIBSYS_OBJECT_CLASS_INTERFACE(NULL),
	.destroy = sys_shared_memory_destroy,
};

const sys_object_class_t* sys_object_class_shared_memory(void) {
	return &shared_memory_class;
};

ferr_t sys_page_allocate(size_t page_count, sys_page_flags_t flags, void** out_address) {
	return sys_page_allocate_advanced(page_count, 0, flags, out_address);
};

ferr_t sys_page_allocate_advanced(size_t page_count, uint8_t alignment_power, sys_page_flags_t flags, void** out_address) {
	return libsyscall_wrapper_page_allocate(page_count, flags, alignment_power, out_address);
};

ferr_t sys_page_free(void* address) {
	return libsyscall_wrapper_page_free(address);
};

ferr_t sys_page_translate(const void* address, uint64_t* out_physical_address) {
	return libsyscall_wrapper_page_translate(address, out_physical_address);
};

ferr_t sys_shared_memory_allocate(size_t page_count, sys_shared_memory_flags_t flags, sys_shared_memory_t** out_shared_memory) {
	ferr_t status = ferr_ok;
	sys_shared_memory_object_t* shared_memory = NULL;

	status = sys_object_new(&shared_memory_class, sizeof(*shared_memory) - sizeof(shared_memory->object), (void*)&shared_memory);
	if (status != ferr_ok) {
		goto out;
	}

	shared_memory->did = UINT64_MAX;

	status = libsyscall_wrapper_page_allocate_shared(page_count, 0, &shared_memory->did);

out:
	if (status == ferr_ok) {
		*out_shared_memory = (void*)shared_memory;
	} else {
		if (shared_memory) {
			sys_release((void*)shared_memory);
		}
	}
	return status;
};

ferr_t sys_shared_memory_map(sys_shared_memory_t* obj, size_t page_count, size_t page_offset_count, void** out_address) {
	ferr_t status = ferr_ok;
	sys_shared_memory_object_t* shared_memory = (void*)obj;
	void* address = NULL;

	status = libsyscall_wrapper_page_map_shared(shared_memory->did, page_count, page_offset_count, 0, 0, &address);

out:
	if (status == ferr_ok) {
		*out_address = address;
	}
	return status;
};

ferr_t sys_shared_memory_bind(sys_shared_memory_t* obj, size_t page_count, size_t page_offset_count, void* address) {
	ferr_t status = ferr_ok;
	sys_shared_memory_object_t* shared_memory = (void*)obj;

	status = libsyscall_wrapper_page_bind_shared(shared_memory->did, page_count, page_offset_count, address);

out:
	return status;
};

ferr_t sys_shared_memory_page_count(sys_shared_memory_t* obj, size_t* out_page_count) {
	ferr_t status = ferr_ok;
	sys_shared_memory_object_t* shared_memory = (void*)obj;
	uint64_t page_count;

	status = libsyscall_wrapper_page_count_shared(shared_memory->did, &page_count);
	if (status != ferr_ok) {
		goto out;
	}

	*out_page_count = page_count;

out:
	return status;
};
