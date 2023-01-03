/*
 * This file is part of Anillo OS
 * Copyright (C) 2023 Anillo OS Developers
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

#include <libsys/data.private.h>
#include <libsys/mempool.h>
#include <libsimple/libsimple.h>
#include <gen/libsyscall/syscall-wrappers.h>

static void sys_data_destroy(sys_object_t* obj) {
	sys_data_object_t* data = (void*)obj;

	if (data->shared_memory) {
		if (data->contents) {
			LIBSYS_WUR_IGNORE(sys_page_free(data->contents));
		}
		sys_release(data->shared_memory);
	} else if (data->owns_contents && data->contents) {
		LIBSYS_WUR_IGNORE(sys_mempool_free(data->contents));
	}

	sys_object_destroy(obj);
};

static const sys_object_class_t data_class = {
	LIBSYS_OBJECT_CLASS_INTERFACE(NULL),
	.destroy = sys_data_destroy,
};

const sys_object_class_t* sys_object_class_data(void) {
	return &data_class;
};

ferr_t sys_data_create(const void* contents, size_t length, sys_data_create_flags_t flags, sys_data_t** out_data) {
	ferr_t status = ferr_ok;
	sys_data_object_t* data = NULL;
	void* contents_ptr = NULL;
	sys_shared_memory_t* shmem = NULL;

	if (flags & sys_data_create_flag_shared) {
		status = sys_shared_memory_allocate(sys_page_round_up_count(length), 0, &shmem);
		if (status != ferr_ok) {
			goto out;
		}
	} else {
		status = sys_mempool_allocate(length, NULL, &contents_ptr);
		if (status != ferr_ok) {
			goto out;
		}
	}

	status = sys_object_new(&data_class, sizeof(*data) - sizeof(data->object), (void*)&data);
	if (status != ferr_ok) {
		goto out;
	}

	data->length = length;
	data->contents = contents_ptr;
	data->owns_contents = true;
	data->shared_memory = shmem;

	if (shmem) {
		status = sys_shared_memory_map(shmem, sys_page_round_up_count(length), 0, &data->contents);
		if (status != ferr_ok) {
			goto out;
		}
	}

	if (contents) {
		simple_memcpy(data->contents, contents, length);
	}

out:
	if (status == ferr_ok) {
		*out_data = (void*)data;
	} else if (data) {
		sys_release((void*)data);
	} else {
		if (contents_ptr) {
			LIBSYS_WUR_IGNORE(sys_mempool_free(contents_ptr));
		}
		if (shmem) {
			sys_release(shmem);
		}
	}
	return status;
};

ferr_t sys_data_create_nocopy(void* contents, size_t length, sys_data_t** out_data) {
	ferr_t status = ferr_ok;
	sys_data_object_t* data = NULL;

	status = sys_object_new(&data_class, sizeof(*data) - sizeof(data->object), (void*)&data);
	if (status != ferr_ok) {
		goto out;
	}

	data->length = length;
	data->contents = contents;
	data->owns_contents = false;
	data->shared_memory = NULL;

out:
	if (status == ferr_ok) {
		*out_data = (void*)data;
	} else if (data) {
		sys_release((void*)data);
	}
	return status;
};

ferr_t sys_data_create_transfer(void* contents, size_t length, sys_data_t** out_data) {
	ferr_t status = ferr_ok;
	sys_data_object_t* data = NULL;

	status = sys_object_new(&data_class, sizeof(*data) - sizeof(data->object), (void*)&data);
	if (status != ferr_ok) {
		goto out;
	}

	data->length = length;
	data->contents = contents;
	data->owns_contents = true;
	data->shared_memory = NULL;

out:
	if (status == ferr_ok) {
		*out_data = (void*)data;
	} else if (data) {
		sys_release((void*)data);
	}
	return status;
};

ferr_t sys_data_copy(sys_data_t* obj, sys_data_t** out_data) {
	sys_data_object_t* other = (void*)obj;
	return sys_data_create(other->contents, other->length, 0, out_data);
};

void* sys_data_contents(sys_data_t* obj) {
	sys_data_object_t* data = (void*)obj;
	return data->contents;
};

size_t sys_data_length(sys_data_t* obj) {
	sys_data_object_t* data = (void*)obj;
	return data->length;
};
