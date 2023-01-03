/*
 * This file is part of Anillo OS
 * Copyright (C) 2022 Anillo OS Developers
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

#include <libvfs/libvfs.private.h>
#include <libsys/objects.private.h>
#include <libsimple/general.h>
#include <libspooky/proxy.private.h>

#define SPOOKYGEN_vfsman_STORAGE static
#include <vfs.client.h>
#include <vfs.client.c>

ferr_t vfs_retain(vfs_object_t* object) {
	return sys_retain(object);
};

void vfs_release(vfs_object_t* object) {
	return sys_release(object);
};

const vfs_object_class_t* vfs_object_class(vfs_object_t* object) {
	return sys_object_class(object);
};

static void vfs_file_destroy(vfs_object_t* obj) {
	vfs_file_object_t* file = (void*)obj;

	if (file->proxy) {
		spooky_release(file->proxy);
	}

	sys_object_destroy(obj);
};

static const vfs_object_class_t vfs_file_class = {
	LIBSYS_OBJECT_CLASS_INTERFACE(NULL),
	.destroy = vfs_file_destroy,
};

const vfs_object_class_t* vfs_object_class_file(void) {
	return &vfs_file_class;
};

ferr_t vfs_open(const char* path, vfs_file_t** out_file) {
	return vfs_open_n(path, simple_strlen(path), out_file);
};

ferr_t vfs_open_n(const char* path, size_t length, vfs_file_t** out_file) {
	ferr_t status = ferr_ok;
	sys_data_t* path_data = NULL;
	vfs_file_object_t* file = NULL;
	ferr_t open_status = ferr_ok;

	status = sys_object_new(&vfs_file_class, sizeof(*file) - sizeof(file->object), (void*)&file);
	if (status != ferr_ok) {
		goto out;
	}

	simple_memset((char*)file + sizeof(file->object), 0, sizeof(*file) - sizeof(file->object));

	// yes, sys_data_create_nocopy takes a non-const pointer, but it's fine.
	// it doesn't modify the data on its own and none of the functions we call with the data modify it either.
	status = sys_data_create_nocopy((void*)path, length, &path_data);
	if (status != ferr_ok) {
		goto out;
	}

	status = vfsman_open(NULL, path_data, &file->proxy, &open_status);
	if (status != ferr_ok) {
		goto out;
	}

	status = open_status;
	if (status != ferr_ok) {
		goto out;
	}

out:
	if (status == ferr_ok) {
		*out_file = (void*)file;
	} else if (file) {
		vfs_release((void*)file);
	}
	if (path_data) {
		spooky_release(path_data);
	}
	if (status == ferr_aborted) {
		status = ferr_should_restart;
	}
	return status;
};

ferr_t vfs_file_read(vfs_file_t* obj, size_t offset, size_t size, void* buffer, size_t* out_read_size) {
	ferr_t status = ferr_ok;
	vfs_file_object_t* file = (void*)obj;
	sys_data_t* buffer_data = NULL;
	ferr_t read_status = ferr_ok;

	status = vfsman_file_read(file->proxy, offset, size, &buffer_data, &read_status);
	if (status != ferr_ok) {
		goto out;
	}

	status = read_status;
	if (status != ferr_ok) {
		goto out;
	}

	simple_memcpy(buffer, sys_data_contents(buffer_data), sys_data_length(buffer_data));

	if (out_read_size) {
		*out_read_size = sys_data_length(buffer_data);
	}

out:
	if (buffer_data) {
		spooky_release(buffer_data);
	}
	if (status == ferr_aborted) {
		status = ferr_should_restart;
	}
	return status;
};

ferr_t vfs_file_read_data(vfs_file_t* obj, size_t offset, size_t size, sys_data_t** out_data) {
	ferr_t status = ferr_ok;
	vfs_file_object_t* file = (void*)obj;
	sys_data_t* buffer_data = NULL;
	ferr_t read_status = ferr_ok;

	status = vfsman_file_read(file->proxy, offset, size, &buffer_data, &read_status);
	if (status != ferr_ok) {
		goto out;
	}

	status = read_status;
	if (status != ferr_ok) {
		goto out;
	}

	*out_data = buffer_data;

out:
	if (status != ferr_ok) {
		if (buffer_data) {
			spooky_release(buffer_data);
		}
	}
	if (status == ferr_aborted) {
		status = ferr_should_restart;
	}
	return status;
};

ferr_t vfs_file_write(vfs_file_t* obj, size_t offset, size_t size, const void* buffer, size_t* out_written_size) {
	ferr_t status = ferr_ok;
	vfs_file_object_t* file = (void*)obj;
	sys_data_t* buffer_data = NULL;
	ferr_t write_status = ferr_ok;
	uint64_t written_count = 0;

	// like in vfs_open_n(), casting the const away here is safe; we never modify the data in this buffer
	status = sys_data_create_nocopy((void*)buffer, size, &buffer_data);
	if (status != ferr_ok) {
		goto out;
	}

	status = vfsman_file_write(file->proxy, offset, buffer_data, &written_count, &write_status);
	if (status != ferr_ok) {
		goto out;
	}

	status = write_status;
	if (status != ferr_ok) {
		goto out;
	}

	if (out_written_size) {
		*out_written_size = written_count;
	}

out:
	if (buffer_data) {
		spooky_release(buffer_data);
	}
	if (status == ferr_aborted) {
		status = ferr_should_restart;
	}
	return status;
};

ferr_t vfs_file_copy_path(vfs_file_t* obj, char* buffer, size_t size, size_t* out_actual_size) {
	ferr_t status = ferr_ok;
	vfs_file_object_t* file = (void*)obj;
	sys_data_t* buffer_data = NULL;
	ferr_t copy_status = ferr_ok;

	status = vfsman_file_get_path(file->proxy, &buffer_data, &copy_status);
	if (status != ferr_ok) {
		goto out;
	}

	status = copy_status;
	if (status != ferr_ok) {
		goto out;
	}

	if (out_actual_size) {
		*out_actual_size = sys_data_length(buffer_data);
	}

	if (sys_data_length(buffer_data) > size) {
		status = ferr_too_big;
	} else {
		simple_memcpy(buffer, sys_data_contents(buffer_data), sys_data_length(buffer_data));
	}

out:
	if (buffer_data) {
		spooky_release(buffer_data);
	}
	if (status == ferr_aborted) {
		status = ferr_should_restart;
	}
	return status;
};

ferr_t vfs_file_duplicate_raw(vfs_file_t* obj, sys_channel_t** out_channel) {
	ferr_t status = ferr_ok;
	vfs_file_object_t* file = (void*)obj;
	ferr_t dup_status = ferr_ok;
	sys_channel_t* channel = NULL;

	status = vfsman_file_duplicate_raw(file->proxy, &channel, &dup_status);
	if (status != ferr_ok) {
		goto out;
	}

	status = dup_status;
	if (status != ferr_ok) {
		goto out;
	}

out:
	if (status == ferr_ok) {
		*out_channel = channel;
	} else if (channel) {
		sys_release(channel);
	}
	if (status == ferr_aborted) {
		status = ferr_should_restart;
	}
	return status;
};

ferr_t vfs_open_raw(sys_channel_t* channel, vfs_file_t** out_file) {
	ferr_t status = ferr_ok;
	vfs_file_object_t* file = NULL;

	status = sys_object_new(&vfs_file_class, sizeof(*file) - sizeof(file->object), (void*)&file);
	if (status != ferr_ok) {
		goto out;
	}

	simple_memset((char*)file + sizeof(file->object), 0, sizeof(*file) - sizeof(file->object));

	status = spooky_proxy_create_incoming(channel, eve_loop_get_main(), &file->proxy);
	if (status != ferr_ok) {
		goto out;
	}

out:
	if (status == ferr_ok) {
		*out_file = (void*)file;
	} else if (file) {
		vfs_release((void*)file);
	}
	if (status == ferr_aborted) {
		status = ferr_should_restart;
	}
	return status;
};
