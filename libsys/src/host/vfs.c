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

#include <libsys/vfs.h>

ferr_t vfs_open_special(vfs_node_special_id_t id, vfs_node_t** out_node) {
	return ferr_unsupported;
};

ferr_t vfs_open(const char* path, vfs_node_t** out_node) {
	return ferr_unsupported;
};

ferr_t vfs_open_n(const char* path, size_t path_length, vfs_node_t** out_node) {
	return ferr_unsupported;
};

ferr_t vfs_node_read(vfs_node_t* node, uint64_t offset, size_t buffer_size, void* out_buffer, size_t* out_read_count) {
	return ferr_unsupported;
};

ferr_t vfs_node_read_data(vfs_node_t* node, uint64_t offset, size_t size, sys_data_t** out_data) {
	return ferr_unsupported;
};

ferr_t vfs_node_read_retry(vfs_node_t* node, uint64_t offset, size_t buffer_size, void* out_buffer, size_t* out_read_count) {
	return ferr_unsupported;
};

ferr_t vfs_node_read_into_shared_data(vfs_node_t* node, uint64_t read_offset, uint64_t shared_data_offset, size_t size, sys_data_t* shared_data, size_t* out_read_count) {
	return ferr_unsupported;
};

ferr_t vfs_node_write(vfs_node_t* node, uint64_t offset, size_t buffer_size, const void* buffer, size_t* out_written_count) {
	return ferr_unsupported;
};

ferr_t vfs_node_copy_path(vfs_node_t* node, size_t buffer_size, void* out_buffer, size_t* out_actual_size) {
	return ferr_unsupported;
};

ferr_t vfs_node_get_info(vfs_node_t* node, vfs_node_info_t* out_info) {
	return ferr_unsupported;
};

ferr_t vfs_node_copy_path_allocate(vfs_node_t* node, char** out_string, size_t* out_string_length) {
	return ferr_unsupported;
};
