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

#ifndef _LIBVFS_LIBVFS_H_
#define _LIBVFS_LIBVFS_H_

#include <stdint.h>
#include <stddef.h>

#include <libvfs/base.h>
#include <libsys/objects.h>
#include <libsys/data.h>

typedef sys_object_t vfs_object_t;
typedef sys_object_class_t vfs_object_class_t;

LIBSYS_WUR ferr_t vfs_retain(vfs_object_t* object);
void vfs_release(vfs_object_t* object);

#define LIBVFS_OBJECT_CLASS(_name) \
	typedef vfs_object_t vfs_ ## _name ## _t; \
	const vfs_object_class_t* vfs_object_class_ ## _name (void);

const vfs_object_class_t* vfs_object_class(vfs_object_t* object);

LIBVFS_OBJECT_CLASS(node);
LIBVFS_OBJECT_CLASS(listing);

LIBVFS_ENUM(uint8_t, vfs_node_type) {
	vfs_node_type_invalid = 0,
	vfs_node_type_file,
	vfs_node_type_directory,
};

LIBVFS_STRUCT(vfs_node_info) {
	size_t size;
	vfs_node_type_t type;
	char _reserved[7];
};

LIBVFS_STRUCT(vfs_directory_entry) {
	vfs_node_info_t info;
	size_t offset_to_next;
	size_t name_length;
	char name[];
};

#define vfs_directory_entry_get_next(_entry) ({ \
		__typeof__(entry) _tmp = (_entry); \
		(_tmp->offset_to_next == 0) ? NULL : (__typeof__(entry))((uintptr_t)_tmp + _tmp->offset_to_next); \
	})

LIBVFS_ALWAYS_INLINE const char* vfs_directory_entry_get_name(const vfs_directory_entry_t* entry) {
	return entry->name;
};

LIBVFS_WUR ferr_t vfs_open(const char* path, vfs_node_t** out_node);
LIBVFS_WUR ferr_t vfs_open_n(const char* path, size_t length, vfs_node_t** out_node);
LIBVFS_WUR ferr_t vfs_get_path_info(const char* path, vfs_node_info_t* out_info);
LIBVFS_WUR ferr_t vfs_get_path_info_n(const char* path, size_t length, vfs_node_info_t* out_info);

// common functions
LIBVFS_WUR ferr_t vfs_node_copy_path(vfs_node_t* node, size_t size, void* out_buffer, size_t* out_actual_size);
LIBVFS_WUR ferr_t vfs_node_get_info(vfs_node_t* node, vfs_node_info_t* out_info);

// file functions
LIBVFS_WUR ferr_t vfs_node_read(vfs_node_t* node, uint64_t offset, size_t buffer_size, void* out_buffer, size_t* out_read_size);
LIBVFS_WUR ferr_t vfs_node_read_data(vfs_node_t* node, uint64_t offset, size_t size, sys_data_t** out_data);
LIBVFS_WUR ferr_t vfs_node_read_into_shared_data(vfs_node_t* node, uint64_t read_offset, uint64_t shared_data_offset, size_t size, sys_data_t* shared_data, size_t* out_read_size);
LIBVFS_WUR ferr_t vfs_node_write(vfs_node_t* node, uint64_t offset, size_t size, const void* buffer, size_t* out_written_size);

// directory functions
LIBVFS_WUR ferr_t vfs_node_list(vfs_node_t* node, vfs_listing_t** out_listing);

LIBVFS_WUR ferr_t vfs_listing_next(vfs_listing_t* listing, size_t max_entries, void* buffer, size_t buffer_size, size_t* out_entry_count, size_t* out_min_buffer_size);
LIBVFS_WUR ferr_t vfs_listing_next_data(vfs_listing_t* listing, size_t max_entries, size_t max_buffer_size, sys_data_t** out_data, size_t* out_entry_count, size_t* out_min_buffer_size);
LIBVFS_WUR ferr_t vfs_listing_next_into_shared_data(vfs_listing_t* listing, size_t max_entries, size_t max_buffer_size, sys_data_t* shared_data, size_t shared_data_offset, size_t* out_entry_count, size_t* out_min_buffer_size);

#endif // _LIBVFS_LIBVFS_H_
