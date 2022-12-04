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

#include <libvfs/base.h>

typedef sys_object_t vfs_object_t;
typedef sys_object_class_t vfs_object_class_t;

LIBSYS_WUR ferr_t vfs_retain(vfs_object_t* object);
void vfs_release(vfs_object_t* object);

#define LIBVFS_OBJECT_CLASS(_name) \
	typedef vfs_object_t vfs_ ## _name ## _t; \
	const vfs_object_class_t* vfs_object_class_ ## _name (void);

const vfs_object_class_t* vfs_object_class(vfs_object_t* object);

LIBVFS_OBJECT_CLASS(file);

LIBVFS_WUR ferr_t vfs_open(const char* path, vfs_file_t** out_file);
LIBVFS_WUR ferr_t vfs_open_n(const char* path, size_t length, vfs_file_t** out_file);

LIBVFS_WUR ferr_t vfs_file_read(vfs_file_t* file, size_t offset, size_t size, void* buffer, size_t* out_read_size);
LIBVFS_WUR ferr_t vfs_file_write(vfs_file_t* file, size_t offset, size_t size, const void* buffer, size_t* out_written_size);
LIBVFS_WUR ferr_t vfs_file_copy_path(vfs_file_t* file, char* buffer, size_t size, size_t* out_actual_size);

#endif // _LIBVFS_LIBVFS_H_
