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

#ifndef _LIBSYS_FILES_H_
#define _LIBSYS_FILES_H_

#include <stdint.h>
#include <stddef.h>

#include <libsys/base.h>
#include <ferro/error.h>
#include <libsys/objects.h>

LIBSYS_DECLARATIONS_BEGIN;

LIBSYS_ENUM(uint64_t, sys_file_special_id) {
	sys_file_special_id_process_binary,
};

LIBSYS_OBJECT_CLASS(sys_file);

typedef uint64_t sys_fd_t;

#define SYS_FD_INVALID UINT64_MAX

LIBSYS_WUR ferr_t sys_file_fd(sys_file_t* file, sys_fd_t* out_fd);

/**
 * Create a new file object from an existing descriptor.
 * Ownership of the descriptor is transferred to the new file object.
 *
 * @retval ferr_ok               The file object was successfully created and the caller has been granted a single reference on it.
 * @retval ferr_invalid_argument One or more of: 1) @p fd was an invalid descriptor, 2) @p out_file was `NULL`.
 * @retval ferr_temporary_outage There were insufficient resources to create the file object.
 *
 * @note Upon successful creation, ownership of the descriptor is moved into the file object.
 *       The caller must NOT close the descriptor.
 */
LIBSYS_WUR ferr_t sys_file_from_fd(sys_fd_t fd, sys_file_t** out_file);

LIBSYS_WUR ferr_t sys_file_open_special(sys_file_special_id_t id, sys_file_t** out_file);
LIBSYS_WUR ferr_t sys_file_open_special_fd(sys_file_special_id_t id, sys_fd_t* out_fd);

LIBSYS_WUR ferr_t sys_file_open(const char* path, sys_file_t** out_file);
LIBSYS_WUR ferr_t sys_file_open_fd(const char* path, sys_fd_t* out_fd);
LIBSYS_WUR ferr_t sys_file_open_n(const char* path, size_t path_length, sys_file_t** out_file);
LIBSYS_WUR ferr_t sys_file_open_fd_n(const char* path, size_t path_length, sys_fd_t* out_fd);

LIBSYS_WUR ferr_t sys_file_close_fd(sys_fd_t fd);

LIBSYS_WUR ferr_t sys_file_read(sys_file_t* file, uint64_t offset, size_t buffer_size, void* out_buffer, size_t* out_read_count);
LIBSYS_WUR ferr_t sys_file_read_fd(sys_fd_t fd, uint64_t offset, size_t buffer_size, void* out_buffer, size_t* out_read_count);

LIBSYS_WUR ferr_t sys_file_read_retry(sys_file_t* file, uint64_t offset, size_t buffer_size, void* out_buffer, size_t* out_read_count);
LIBSYS_WUR ferr_t sys_file_read_retry_fd(sys_fd_t fd, uint64_t offset, size_t buffer_size, void* out_buffer, size_t* out_read_count);

LIBSYS_WUR ferr_t sys_file_write(sys_file_t* file, uint64_t offset, size_t buffer_size, const void* buffer, size_t* out_written_count);
LIBSYS_WUR ferr_t sys_file_write_fd(sys_fd_t fd, uint64_t offset, size_t buffer_size, const void* buffer, size_t* out_written_count);

LIBSYS_WUR ferr_t sys_file_copy_path(sys_file_t* file, size_t buffer_size, void* out_buffer, size_t* out_actual_size);
LIBSYS_WUR ferr_t sys_file_copy_path_fd(sys_fd_t fd, size_t buffer_size, void* out_buffer, size_t* out_actual_size);

/**
 * Like sys_file_copy_path(), but automatically allocates a buffer using the memory pool.
 *
 * Upon success, the caller owns the allocated string and must free it back into the memory pool once they're done using it.
 */
LIBSYS_WUR ferr_t sys_file_copy_path_allocate(sys_file_t* file, char** out_string, size_t* out_string_length);
LIBSYS_WUR ferr_t sys_file_copy_path_allocate_fd(sys_fd_t fd, char** out_string, size_t* out_string_length);

LIBSYS_DECLARATIONS_END;

#endif // _LIBSYS_FILES_H_
