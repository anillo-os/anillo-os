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

#include <libsys/files.h>

ferr_t sys_file_open_special(sys_file_special_id_t id, sys_file_t** out_file) {
	return ferr_unsupported;
};

ferr_t sys_file_open(const char* path, sys_file_t** out_file) {
	return ferr_unsupported;
};

ferr_t sys_file_open_n(const char* path, size_t path_length, sys_file_t** out_file) {
	return ferr_unsupported;
};

ferr_t sys_file_read(sys_file_t* file, uint64_t offset, size_t buffer_size, void* out_buffer, size_t* out_read_count) {
	return ferr_unsupported;
};

ferr_t sys_file_read_data(sys_file_t* file, uint64_t offset, size_t size, sys_data_t** out_data) {
	return ferr_unsupported;
};

ferr_t sys_file_read_retry(sys_file_t* file, uint64_t offset, size_t buffer_size, void* out_buffer, size_t* out_read_count) {
	return ferr_unsupported;
};

ferr_t sys_file_read_into_shared_data(sys_file_t* file, uint64_t read_offset, uint64_t shared_data_offset, size_t size, sys_data_t* shared_data, size_t* out_read_count) {
	return ferr_unsupported;
};

ferr_t sys_file_write(sys_file_t* file, uint64_t offset, size_t buffer_size, const void* buffer, size_t* out_written_count) {
	return ferr_unsupported;
};

ferr_t sys_file_copy_path(sys_file_t* file, size_t buffer_size, void* out_buffer, size_t* out_actual_size) {
	return ferr_unsupported;
};

ferr_t sys_file_get_info(sys_file_t* file, sys_file_info_t* out_info) {
	return ferr_unsupported;
};

ferr_t sys_file_copy_path_allocate(sys_file_t* file, char** out_string, size_t* out_string_length) {
	return ferr_unsupported;
};
