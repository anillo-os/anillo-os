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

#include <libvfs/libvfs.h>
#include <libsys/objects.private.h>
#include <libspooky/libspooky.h>

LIBVFS_STRUCT(vfs_file_object) {
	vfs_object_t object;
	spooky_proxy_t* proxy;
};

LIBVFS_WUR ferr_t vfs_file_duplicate_raw(vfs_file_t* file, sys_channel_t** out_channel);
LIBVFS_WUR ferr_t vfs_open_raw(sys_channel_t* channel, vfs_file_t** out_file);
