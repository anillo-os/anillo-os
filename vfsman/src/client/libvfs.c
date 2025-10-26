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
#include <stdint.h>
#include <vfsman/libvfs-common.h>

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

static void vfs_listing_destroy(vfs_object_t* obj) {
	vfs_listing_object_t* node = (void*)obj;

	if (node->proxy) {
		spooky_release(node->proxy);
	}

	sys_object_destroy(obj);
};

static const vfs_object_class_t vfs_listing_class = {
	LIBSYS_OBJECT_CLASS_INTERFACE(NULL),
	.destroy = vfs_listing_destroy,
};

ferr_t vfs_listing_next(vfs_listing_t* obj, size_t max_entries, void* buffer, size_t buffer_size, size_t* out_entry_count, size_t* out_min_buffer_size) {
	ferr_t status = ferr_ok;
	vfs_listing_object_t* listing = (void*)obj;
	ferr_t next_status = ferr_ok;
	sys_data_t* data = NULL;
	uint64_t entry_count = 0;
	uint64_t min_buf_size = 0;

	status = vfsman_listing_next(listing->proxy, max_entries, buffer_size, &data, &entry_count, &min_buf_size, &next_status);
	if (status == ferr_ok) {
		status = next_status;
	}

out:
	if (status == ferr_ok && buffer != NULL) {
		simple_memcpy(buffer, sys_data_contents(data), sys_data_length(data));
		if (out_entry_count) {
			*out_entry_count = entry_count;
		}
	}
	if (out_min_buffer_size) {
		*out_min_buffer_size = min_buf_size;
	}
	if (data) {
		sys_release(data);
	}
	return status;
};

ferr_t vfs_listing_next_data(vfs_listing_t* obj, size_t max_entries, size_t max_buffer_size, sys_data_t** out_data, size_t* out_entry_count, size_t* out_min_buffer_size) {
	ferr_t status = ferr_ok;
	vfs_listing_object_t* listing = (void*)obj;
	ferr_t next_status = ferr_ok;
	uint64_t entry_count = 0;
	uint64_t min_buf_size = 0;

	status = vfsman_listing_next(listing->proxy, max_entries, max_buffer_size, out_data, &entry_count, &min_buf_size, &next_status);
	if (status == ferr_ok) {
		status = next_status;
	}

out:
	if (status == ferr_ok) {
		if (out_entry_count) {
			*out_entry_count = entry_count;
		}
	}
	if (out_min_buffer_size) {
		*out_min_buffer_size = min_buf_size;
	}
	return status;
};

ferr_t vfs_listing_next_into_shared_data(vfs_listing_t* obj, size_t max_entries, size_t max_buffer_size, sys_data_t* shared_data, size_t shared_data_offset, size_t* out_entry_count, size_t* out_min_buffer_size) {
	ferr_t status = ferr_ok;
	vfs_listing_object_t* listing = (void*)obj;
	ferr_t next_status = ferr_ok;
	uint64_t entry_count = 0;
	uint64_t min_buf_size = 0;

	status = vfsman_listing_next_shared(listing->proxy, max_entries, max_buffer_size, shared_data, shared_data_offset, &entry_count, NULL, &min_buf_size, &next_status);
	if (status == ferr_ok) {
		status = next_status;
	}

out:
	if (status == ferr_ok) {
		if (out_entry_count) {
			*out_entry_count = entry_count;
		}
	}
	if (out_min_buffer_size) {
		*out_min_buffer_size = min_buf_size;
	}
	return status;
};

static void vfs_node_destroy(vfs_object_t* obj) {
	vfs_node_object_t* node = (void*)obj;

	if (node->proxy) {
		spooky_release(node->proxy);
	}

	sys_object_destroy(obj);
};

static const vfs_object_class_t vfs_node_class = {
	LIBSYS_OBJECT_CLASS_INTERFACE(NULL),
	.destroy = vfs_node_destroy,
};

const vfs_object_class_t* vfs_object_class_node(void) {
	return &vfs_node_class;
};

ferr_t vfs_open(const char* path, vfs_node_t** out_node) {
	return vfs_open_n(path, simple_strlen(path), out_node);
};

ferr_t vfs_open_n(const char* path, size_t length, vfs_node_t** out_node) {
	ferr_t status = ferr_ok;
	sys_data_t* path_data = NULL;
	vfs_node_object_t* node = NULL;
	ferr_t open_status = ferr_ok;

	status = sys_object_new(&vfs_node_class, sizeof(*node) - sizeof(node->object), (void*)&node);
	if (status != ferr_ok) {
		goto out;
	}

	simple_memset((char*)node + sizeof(node->object), 0, sizeof(*node) - sizeof(node->object));

	// yes, sys_data_create_nocopy takes a non-const pointer, but it's fine.
	// it doesn't modify the data on its own and none of the functions we call with the data modify it either.
	status = sys_data_create_nocopy((void*)path, length, &path_data);
	if (status != ferr_ok) {
		goto out;
	}

	status = vfsman_open(NULL, path_data, &node->proxy, &open_status);
	if (status != ferr_ok) {
		goto out;
	}

	status = open_status;
	if (status != ferr_ok) {
		goto out;
	}

out:
	if (status == ferr_ok) {
		*out_node = (void*)node;
	} else if (node) {
		vfs_release((void*)node);
	}
	if (path_data) {
		spooky_release(path_data);
	}
	if (status == ferr_aborted) {
		status = ferr_should_restart;
	}
	return status;
};

ferr_t vfs_node_read(vfs_node_t* obj, uint64_t offset, size_t size, void* buffer, size_t* out_read_size) {
	ferr_t status = ferr_ok;
	vfs_node_object_t* node = (void*)obj;
	sys_data_t* buffer_data = NULL;
	ferr_t read_status = ferr_ok;

	status = vfsman_node_read(node->proxy, offset, size, &buffer_data, &read_status);
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

ferr_t vfs_node_read_data(vfs_node_t* obj, uint64_t offset, size_t size, sys_data_t** out_data) {
	ferr_t status = ferr_ok;
	vfs_node_object_t* node = (void*)obj;
	sys_data_t* buffer_data = NULL;
	ferr_t read_status = ferr_ok;

	status = vfsman_node_read(node->proxy, offset, size, &buffer_data, &read_status);
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

ferr_t vfs_node_read_into_shared_data(vfs_node_t* obj, uint64_t read_offset, uint64_t shared_data_offset, size_t size, sys_data_t* shared_data, size_t* out_read_size) {
	ferr_t status = ferr_ok;
	vfs_node_object_t* node = (void*)obj;
	uint64_t read_count = 0;
	ferr_t read_status = ferr_ok;

	status = vfsman_node_read_shared(node->proxy, read_offset, size, shared_data, shared_data_offset, &read_count, &read_status);
	if (status != ferr_ok) {
		goto out;
	}

	status = read_status;
	if (status != ferr_ok) {
		goto out;
	}

	if (out_read_size) {
		*out_read_size = read_count;
	}

out:
	if (status == ferr_aborted) {
		status = ferr_should_restart;
	}
	return status;
};

ferr_t vfs_node_write(vfs_node_t* obj, uint64_t offset, size_t size, const void* buffer, size_t* out_written_size) {
	ferr_t status = ferr_ok;
	vfs_node_object_t* node = (void*)obj;
	sys_data_t* buffer_data = NULL;
	ferr_t write_status = ferr_ok;
	uint64_t written_count = 0;

	// like in vfs_open_n(), casting the const away here is safe; we never modify the data in this buffer
	status = sys_data_create_nocopy((void*)buffer, size, &buffer_data);
	if (status != ferr_ok) {
		goto out;
	}

	status = vfsman_node_write(node->proxy, offset, buffer_data, &written_count, &write_status);
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

ferr_t vfs_node_copy_path(vfs_node_t* obj, size_t size, void* out_buffer, size_t* out_actual_size) {
	ferr_t status = ferr_ok;
	vfs_node_object_t* node = (void*)obj;
	sys_data_t* buffer_data = NULL;
	ferr_t copy_status = ferr_ok;

	status = vfsman_node_get_path(node->proxy, &buffer_data, &copy_status);
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
		simple_memcpy(out_buffer, sys_data_contents(buffer_data), sys_data_length(buffer_data));
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

ferr_t vfs_node_duplicate_raw(vfs_node_t* obj, sys_channel_t** out_channel) {
	ferr_t status = ferr_ok;
	vfs_node_object_t* node = (void*)obj;
	ferr_t dup_status = ferr_ok;
	sys_channel_t* channel = NULL;

	status = vfsman_node_duplicate_raw(node->proxy, &channel, &dup_status);
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

ferr_t vfs_node_get_info(vfs_node_t* obj, vfs_node_info_t* out_info) {
	ferr_t status = ferr_ok;
	ferr_t get_status = ferr_ok;
	vfs_node_object_t* node = (void*)obj;
	struct vfsman_path_info vfsman_info;

	status = vfsman_node_get_info(node->proxy, &vfsman_info, &get_status);
	if (status == ferr_ok) {
		status = get_status;
	}
	if (status != ferr_ok) {
		goto out;
	}

	if (out_info) {
		out_info->size = vfsman_info.size;
		out_info->type = vfsman_info.type;
	}

out:
	return status;
};

ferr_t vfs_node_list(vfs_node_t* obj, vfs_listing_t** out_listing) {
	ferr_t status = ferr_ok;
	ferr_t list_status = ferr_ok;
	vfs_node_object_t* node = (void*)obj;
	vfs_listing_object_t* listing = NULL;

	status = sys_object_new(&vfs_listing_class, sizeof(*listing) - sizeof(listing->object), (void*)&listing);
	if (status != ferr_ok) {
		goto out;
	}

	simple_memset((char*)listing + sizeof(listing->object), 0, sizeof(*listing) - sizeof(listing->object));

	status = vfsman_node_list(node->proxy, &listing->proxy, &list_status);
	if (status == ferr_ok) {
		status = list_status;
	}
	if (status != ferr_ok) {
		goto out;
	}

out:
	if (status == ferr_ok) {
		*out_listing = (void*)listing;
	} else if (listing) {
		vfs_release((void*)listing);
	}
	if (status == ferr_aborted) {
		status = ferr_should_restart;
	}
	return status;
};

ferr_t vfs_open_raw(sys_channel_t* channel, vfs_node_t** out_node) {
	ferr_t status = ferr_ok;
	vfs_node_object_t* node = NULL;

	status = sys_object_new(&vfs_node_class, sizeof(*node) - sizeof(node->object), (void*)&node);
	if (status != ferr_ok) {
		goto out;
	}

	simple_memset((char*)node + sizeof(node->object), 0, sizeof(*node) - sizeof(node->object));

	status = spooky_proxy_create_incoming(channel, eve_loop_get_main(), &node->proxy);
	if (status != ferr_ok) {
		goto out;
	}

out:
	if (status == ferr_ok) {
		*out_node = (void*)node;
	} else if (node) {
		vfs_release((void*)node);
	}
	if (status == ferr_aborted) {
		status = ferr_should_restart;
	}
	return status;
};

ferr_t vfs_get_path_info(const char* path, vfs_node_info_t* out_info) {
	return vfs_get_path_info_n(path, simple_strlen(path), out_info);
};

ferr_t vfs_get_path_info_n(const char* path, size_t length, vfs_node_info_t* out_info) {
	ferr_t status = ferr_ok;
	ferr_t info_status = ferr_ok;
	sys_data_t* path_data = NULL;
	struct vfsman_path_info vfsman_info;

	status = sys_data_create_nocopy((void*)path, length, &path_data);
	if (status != ferr_ok) {
		goto out;
	}

	status = vfsman_get_path_info(NULL, path_data, &vfsman_info, &info_status);
	if (status == ferr_ok) {
		status = info_status;
	}
	if (status != ferr_ok) {
		goto out;
	}

	if (out_info) {
		out_info->size = vfsman_info.size;
		out_info->type = vfsman_info.type;
	}

out:
	if (path_data) {
		spooky_release(path_data);
	}
	if (status == ferr_aborted) {
		status = ferr_should_restart;
	}
	return status;
};
