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

#include <vfsman/vfs.h>
#include <vfs.server.h>
#include <vfsman/vfs.backend.h>
#include <vfsman/objects.h>
#include <libspooky/proxy.private.h>
#include <vfsman/vfs.backend.private.h>
#include <vfsman/server.h>

// it's faster to copy small buffers than to setup shared memory for them
// (note that i have no hard evidence to back this threshold value up, but it's a pretty good guess)
#define VFS_SMALL_BUFFER_THRESHOLD_BYTES (2048ull)

// limit non-shared read buffers to 4MiB per read
#define VFS_MAX_READ_BUFFER (4ull * 1024ull * 1024ull)

// limit non-shared listing buffers to 32KiB per read
#define VFS_MAX_LISTING_BUFFER (32ull * 1024ull)

#define VFS_MAX_LISTING_TRIES 5

static const vfsman_listing_proxy_info_t vfsman_listing_proxy_info_base;
static const vfsman_node_proxy_info_t vfsman_node_proxy_info_base;

static ferr_t vfsman_listing_next_common(vfsman_listing_context_t* context, void* buffer, size_t capacity, size_t* out_used_size, size_t* out_entry_count, size_t* out_min_buffer_size) {
	ferr_t status = ferr_ok;
	vfsman_descriptor_object_t* descriptor = (void*)context->descriptor;
	size_t entry_count = 0;
	size_t used_size = 0;
	size_t min_buf_size = 0;
	bool is_first = true;
	vfs_directory_entry_t* last_entry = NULL;

	eve_mutex_lock(&context->mutex);

	while (true) {
		const sys_path_t* entry = NULL;
		size_t entry_size = 0;
		vfs_directory_entry_t* dir_entry = NULL;
		vfsman_node_info_t node_info;

		if (context->offset == context->count) {
			size_t tries = 0;

retry_list:
			status = descriptor->mount->backend->list_children(descriptor->mount->context, (void*)descriptor, context->children, sizeof(context->children) / sizeof(*context->children), false, &context->count, &context->listing_context);
			if (status == ferr_temporary_outage) {
				++tries;
				if (tries != VFS_MAX_LISTING_TRIES) {
					goto retry_list;
				}
			}
			if (status == ferr_permanent_outage) {
				// we're done listing entries; this is not actually an error
				status = ferr_ok;
				break;
			}
			if (status != ferr_ok) {
				goto out;
			}
			context->offset = 0;
		}

		entry = &context->children[context->offset];
		// round the name length to a multiple of the structure's alignment to keep the entries aligned in the buffer
		entry_size = sizeof(vfs_directory_entry_t) + ((entry->length + (_Alignof(vfs_directory_entry_t) - 1)) & ~(_Alignof(vfs_directory_entry_t) - 1));

		if (is_first) {
			min_buf_size = entry_size;
		}

		if (capacity - used_size < entry_size) {
			// the buffer isn't big enough
			break;
		}

		{
			ferr_t tmp_status = ferr_ok;
			vfsman_descriptor_object_t* child_desc = NULL;

			tmp_status = vfsman_open_rn((void*)descriptor, entry->contents, entry->length, 0, (void*)&child_desc);
			if (is_first) {
				status = tmp_status;
			}
			if (tmp_status == ferr_ok) {
				tmp_status = child_desc->mount->backend->copy_info(child_desc->mount->context, (void*)child_desc, &node_info);
				vfsman_release((void*)child_desc);
				if (is_first) {
					status = tmp_status;
				}
				if (tmp_status != ferr_ok) {
					break;
				}
			} else if (tmp_status == ferr_no_such_resource) {
				// this entry went away, so let's skip it
				++context->offset;
				if (is_first) {
					status = ferr_ok;
				}
				continue;
			} else {
				// some other error
				break;
			}
		}

		dir_entry = (vfs_directory_entry_t*)((uintptr_t)buffer + used_size);
		last_entry = dir_entry;

		++entry_count;
		++context->offset;
		used_size += entry_size;

		dir_entry->offset_to_next = entry_size;
		dir_entry->info.type = node_info.type;
		dir_entry->info.size = node_info.size;
		dir_entry->name_length = entry->length;
		simple_memcpy(dir_entry->name, entry->contents, entry->length);

		if (is_first) {
			is_first = false;
		}
	}

out:
	sys_mutex_unlock(&context->mutex);

	if (last_entry) {
		last_entry->offset_to_next = 0;
	}
	*out_used_size = used_size;
	*out_entry_count = entry_count;
	*out_min_buffer_size = min_buf_size;
	if (status != ferr_permanent_outage && entry_count == 0) {
		status = ferr_too_big;
	}
	return status;
};

static ferr_t vfsman_listing_next_impl(void* _context, uint64_t max_entries, uint64_t max_buffer_size, sys_data_t** out_buffer, uint64_t* out_entry_count, uint64_t* out_min_buffer_size, ferr_t* out_status) {
	ferr_t status = ferr_ok;
	vfsman_listing_context_t* context = _context;
	vfsman_descriptor_object_t* descriptor = (void*)context->descriptor;
	void* buffer = NULL;
	sys_shared_memory_t* shmem = NULL;
	void* mapping = NULL;
	size_t used_size = 0;
	size_t entry_count = 0;
	size_t min_buf_size = 0;

	// limit buffer size to 32KiB
	max_buffer_size = simple_min(max_buffer_size, VFS_MAX_LISTING_BUFFER);

	if (max_buffer_size < VFS_SMALL_BUFFER_THRESHOLD_BYTES) {
		status = sys_mempool_allocate(max_buffer_size, NULL, &buffer);
	} else {
		status = sys_shared_memory_allocate(sys_page_round_up_count(max_buffer_size), 0, &shmem);
		if (status != ferr_ok) {
			goto out;
		}

		status = sys_shared_memory_map(shmem, sys_page_round_up_count(max_buffer_size), 0, &mapping);
	}

	if (status != ferr_ok) {
		goto out;
	}

	status = vfsman_listing_next_common(context, mapping ? mapping : buffer, max_buffer_size, &used_size, &entry_count, &min_buf_size);
	if (status != ferr_ok) {
		goto out;
	}

	if (mapping) {
		status = sys_data_create_from_shared_memory(shmem, 0, used_size, out_buffer);
	} else {
		status = sys_data_create_transfer(buffer, used_size, out_buffer);
		buffer = NULL;
	}

out:
	if (status != ferr_ok) {
		*out_buffer = NULL;
	}
	if (mapping) {
		LIBVFS_WUR_IGNORE(sys_page_free(mapping));
	}
	if (shmem) {
		sys_release(shmem);
	}
	if (buffer) {
		LIBVFS_WUR_IGNORE(sys_mempool_free(buffer));
	}
	*out_entry_count = entry_count;
	*out_min_buffer_size = min_buf_size;
	*out_status = status;
	return ferr_ok;
};

static ferr_t vfsman_listing_next_shared_impl(void* _context, uint64_t max_entries, uint64_t max_buffer_size, sys_data_t* shared_buffer, uint64_t buffer_offset, uint64_t* out_entry_count, uint64_t* out_used_buffer_size, uint64_t* out_min_buffer_size, ferr_t* out_status) {
	vfsman_listing_context_t* context = _context;
	vfsman_descriptor_object_t* descriptor = (void*)context->descriptor;
	ferr_t status = ferr_ok;
	size_t used_size = 0;
	size_t entry_count = 0;
	size_t min_buf_size = 0;

	if (status != ferr_ok) {
		goto out;
	}

	// limit buffer size to capacity of shared buffer
	max_buffer_size = simple_min(max_buffer_size, sys_data_length(shared_buffer) - buffer_offset);

	status = vfsman_listing_next_common(context, (char*)sys_data_contents(shared_buffer) + buffer_offset, max_buffer_size, &used_size, &entry_count, &min_buf_size);
	if (status != ferr_ok) {
		goto out;
	}

out:
	*out_entry_count = entry_count;
	*out_used_buffer_size = used_size;
	*out_min_buffer_size = min_buf_size;
	*out_status = status;
	return ferr_ok;
};

static void vfsman_listing_destroy(void* _context) {
	vfsman_listing_context_t* context = _context;
	vfsman_descriptor_object_t* descriptor = (void*)context->descriptor;

	if (context->listing_context) {
		descriptor->mount->backend->list_children_finish(descriptor->mount->context, (void*)descriptor, context->children, context->count, &context->listing_context);
	}

	if (context->descriptor) {
		vfsman_release(context->descriptor);
	}

	LIBVFS_WUR_IGNORE(sys_mempool_free(context));
};

static const vfsman_listing_proxy_info_t vfsman_listing_proxy_info_base = {
	.context = NULL,
	.destructor = vfsman_listing_destroy,
	.next = vfsman_listing_next_impl,
	.next_shared = vfsman_listing_next_shared_impl,
};

static ferr_t vfsman_node_read_impl(void* _context, uint64_t offset, uint64_t size, sys_data_t** out_buffer, ferr_t* out_status) {
	vfsman_descriptor_t* descriptor = _context;
	void* data = NULL;
	ferr_t status = ferr_ok;
	size_t read_count = 0;
	sys_shared_memory_t* shmem = NULL;
	void* mapping = NULL;

	size = simple_min(size, VFS_MAX_READ_BUFFER);

	if (size < VFS_SMALL_BUFFER_THRESHOLD_BYTES) {
		status = sys_mempool_allocate(size, NULL, &data);
	} else {
		status = sys_shared_memory_allocate(sys_page_round_up_count(size), 0, &shmem);
		if (status != ferr_ok) {
			goto out;
		}

		status = sys_shared_memory_map(shmem, sys_page_round_up_count(size), 0, &mapping);
	}

	if (status != ferr_ok) {
		goto out;
	}

	status = vfsman_read(descriptor, offset, mapping ? mapping : data, size, &read_count);
	if (status != ferr_ok) {
		goto out;
	}

	if (mapping) {
		status = sys_data_create_from_shared_memory(shmem, 0, read_count, out_buffer);
	} else {
		status = sys_data_create_transfer(data, read_count, out_buffer);
		data = NULL;
	}

out:
	if (status != ferr_ok) {
		*out_buffer = NULL;
	}
	if (mapping) {
		LIBVFS_WUR_IGNORE(sys_page_free(mapping));
	}
	if (shmem) {
		sys_release(shmem);
	}
	if (data) {
		LIBVFS_WUR_IGNORE(sys_mempool_free(data));
	}
	*out_status = status;
	return ferr_ok;
};

ferr_t vfsman_node_read_shared_impl(void* _context, uint64_t offset, uint64_t size, sys_data_t* shared_buffer, uint64_t buffer_offset, uint64_t* out_read_count, int32_t* out_status) {
	vfsman_descriptor_t* descriptor = _context;
	ferr_t status = ferr_ok;
	size_t read_count = 0;

	if (status != ferr_ok) {
		goto out;
	}

	// limit buffer size to capacity of shared buffer
	size = simple_min(size, sys_data_length(shared_buffer) - buffer_offset);

	status = vfsman_read(descriptor, offset, (char*)sys_data_contents(shared_buffer) + buffer_offset, size, &read_count);
	if (status != ferr_ok) {
		goto out;
	}

out:
	*out_read_count = read_count;
	*out_status = status;
	return ferr_ok;
};

static ferr_t vfsman_node_write_impl(void* _context, uint64_t offset, sys_data_t* buffer, uint64_t* out_written_count, ferr_t* out_status) {
	vfsman_descriptor_t* descriptor = _context;
	ferr_t status = ferr_ok;
	size_t written_count = 0;

	status = vfsman_write(descriptor, offset, sys_data_contents(buffer), sys_data_length(buffer), &written_count);
	if (status != ferr_ok) {
		goto out;
	}

	*out_written_count = written_count;

out:
	*out_status = status;
	return ferr_ok;
};

static ferr_t vfsman_node_get_path_impl(void* _context, sys_data_t** out_path, ferr_t* out_status) {
	vfsman_descriptor_t* descriptor = _context;
	ferr_t status = ferr_ok;
	size_t buffer_size = 128;
	void* buffer = NULL;
	size_t actual_length = 0;

	status = sys_mempool_allocate(buffer_size, &buffer_size, &buffer);
	if (status != ferr_ok) {
		goto out;
	}

	status = ferr_too_big;
	while (status != ferr_ok) {
		status = vfsman_copy_path(descriptor, true, buffer, buffer_size, &actual_length);
		if (status == ferr_too_big) {
			buffer_size = actual_length;
			status = sys_mempool_reallocate(buffer, buffer_size, &buffer_size, &buffer);
			if (status != ferr_ok) {
				goto out;
			}
			continue;
		} else if (status != ferr_ok) {
			goto out;
		}
		break;
	}

	// try to shrink it
	LIBVFS_WUR_IGNORE(sys_mempool_reallocate(buffer, actual_length, NULL, &buffer));

	status = sys_data_create_transfer(buffer, actual_length, out_path);

out:
	if (status != ferr_ok) {
		*out_path = NULL;
	}
	*out_status = status;
	return ferr_ok;
};

static ferr_t vfsman_node_duplicate_raw_impl(void* _context, sys_channel_t** out_channel, ferr_t* out_status) {
	vfsman_descriptor_t* descriptor = _context;
	ferr_t status = ferr_ok;

	status = spooky_outgoing_proxy_create_channel(((vfsman_descriptor_object_t*)descriptor)->internal_context, out_channel);

out:
	if (status != ferr_ok) {
		*out_channel = NULL;
	}
	*out_status = status;
	return ferr_ok;
};

static ferr_t vfsman_node_get_info_impl(void* _context, struct vfsman_path_info* out_info, ferr_t* out_status) {
	vfsman_descriptor_object_t* descriptor = _context;
	ferr_t status = ferr_ok;
	vfsman_node_info_t node_info;

	status = descriptor->mount->backend->copy_info(descriptor->mount->context, (void*)descriptor, &node_info);
	if (status != ferr_ok) {
		goto out;
	}

	out_info->size = node_info.size;
	out_info->type = node_info.type;

out:
	*out_status = status;
	return ferr_ok;
};

static ferr_t vfsman_node_list_impl(void* _context, spooky_proxy_t** out_listing, ferr_t* out_status) {
	vfsman_descriptor_object_t* descriptor = _context;
	ferr_t status = ferr_ok;
	vfsman_listing_proxy_info_t proxy_info;
	vfsman_listing_context_t* context;
	bool finish_listing_on_fail = false;

	status = sys_mempool_allocate(sizeof(*context), NULL, (void**)&context);
	if (status != ferr_ok) {
		goto out;
	}

	simple_memset(context, 0, sizeof(*context));

	status = vfsman_retain((void*)descriptor);
	if (status != ferr_ok) {
		goto out;
	}

	context->descriptor = (void*)descriptor;
	sys_mutex_init(&context->mutex);

	status = descriptor->mount->backend->list_children_init(descriptor->mount->context, (void*)descriptor, context->children, sizeof(context->children) / sizeof(*context->children), false, &context->count, &context->listing_context);
	if (status != ferr_ok) {
		goto out;
	}
	finish_listing_on_fail = true;

	simple_memcpy(&proxy_info, &vfsman_listing_proxy_info_base, sizeof(proxy_info));
	proxy_info.context = context;

	status = vfsman_listing_create_proxy(&proxy_info, out_listing);
	if (status == ferr_ok) {
		context = NULL;
	}

out:
	if (status != ferr_ok) {
		*out_listing = NULL;
	}
	if (context) {
		if (finish_listing_on_fail) {
			descriptor->mount->backend->list_children_finish(descriptor->mount->context, (void*)descriptor, context->children, context->count, &context->listing_context);
		}
		if (context->descriptor) {
			vfsman_release(context->descriptor);
		}
		LIBVFS_WUR_IGNORE(sys_mempool_free(context));
	}
	*out_status = status;
	return ferr_ok;
};

static const vfsman_node_proxy_info_t vfsman_node_proxy_info_base = {
	.context = NULL,
	.destructor = (void*)vfsman_release,
	.read = vfsman_node_read_impl,
	.read_shared = vfsman_node_read_shared_impl,
	.write = vfsman_node_write_impl,
	.get_path = vfsman_node_get_path_impl,
	.duplicate_raw = vfsman_node_duplicate_raw_impl,
	.get_info = vfsman_node_get_info_impl,
	.list = vfsman_node_list_impl,
};

ferr_t vfsman_open_impl(void* _context, sys_data_t* path, spooky_proxy_t** out_node, ferr_t* out_status) {
	ferr_t status = ferr_ok;
	vfsman_descriptor_t* desc = NULL;
	vfsman_node_proxy_info_t proxy_info;

	status = vfsman_open_n(sys_data_contents(path), sys_data_length(path), 0, &desc);
	if (status != ferr_ok) {
		goto out;
	}

	simple_memcpy(&proxy_info, &vfsman_node_proxy_info_base, sizeof(proxy_info));
	proxy_info.context = desc;
	desc = NULL;

	status = vfsman_node_create_proxy(&proxy_info, out_node);

	if (status == ferr_ok) {
		((vfsman_descriptor_object_t*)proxy_info.context)->internal_context = *out_node;
	}

out:
	if (status != ferr_ok) {
		*out_node = NULL;
	}
	if (desc) {
		vfsman_release(desc);
	}
	*out_status = status;
	return ferr_ok;
};

ferr_t vfsman_list_path_impl(void* _context, sys_data_t* path, spooky_proxy_t** out_listing, ferr_t* out_status) {
	ferr_t status = ferr_ok;
	vfsman_descriptor_object_t* desc = NULL;

	status = vfsman_open_n(sys_data_contents(path), sys_data_length(path), 0, (void*)&desc);
	if (status != ferr_ok) {
		goto out;
	}

	vfsman_node_list_impl(desc, out_listing, &status);

out:
	if (desc) {
		vfsman_release((void*)desc);
	}
	*out_status = status;
	return ferr_ok;
};

ferr_t vfsman_get_path_info_impl(void* _context, sys_data_t* path, struct vfsman_path_info* out_info, ferr_t* out_status) {
	ferr_t status = ferr_ok;
	vfsman_descriptor_object_t* desc = NULL;
	
	status = vfsman_open_n(sys_data_contents(path), sys_data_length(path), 0, (void*)&desc);
	if (status != ferr_ok) {
		goto out;
	}

	vfsman_node_get_info_impl(desc, out_info, &status);

out:
	if (desc) {
		vfsman_release((void*)desc);
	}
	*out_status = status;
	return ferr_ok;
};
