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

#ifndef _VFSMAN_SERVER_H_
#define _VFSMAN_SERVER_H_

#include <libvfs/base.h>
#include <vfsman/vfs.h>

LIBVFS_DECLARATIONS_BEGIN;

LIBVFS_STRUCT(vfsman_listing_context) {
	sys_mutex_t mutex;
	vfsman_descriptor_t* descriptor;
	vfsman_list_children_context_t listing_context;
	sys_path_t children[32];
	size_t count;
	size_t offset;
};

LIBVFS_DECLARATIONS_END;

#endif // _VFSMAN_SERVER_H_
