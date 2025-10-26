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
#include <libsys/data.h>

#include <libvfs/libvfs.h>

LIBSYS_DECLARATIONS_BEGIN;

LIBSYS_ENUM(uint64_t, vfs_node_special_id) {
	vfs_node_special_id_process_binary,
};

LIBSYS_WUR ferr_t vfs_open_special(vfs_node_special_id_t id, vfs_node_t** out_node);

LIBSYS_WUR ferr_t vfs_node_read_retry(vfs_node_t* node, uint64_t offset, size_t buffer_size, void* out_buffer, size_t* out_read_count);

/**
 * Like vfs_node_copy_path(), but automatically allocates a buffer using the memory pool.
 *
 * Upon success, the caller owns the allocated string and must free it back into the memory pool once they're done using it.
 */
LIBSYS_WUR ferr_t vfs_node_copy_path_allocate(vfs_node_t* node, char** out_string, size_t* out_string_length);

LIBSYS_DECLARATIONS_END;

#endif // _LIBSYS_FILES_H_
