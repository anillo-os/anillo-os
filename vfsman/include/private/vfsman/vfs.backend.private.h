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

#ifndef _VFSMAN_VFS_BACKEND_PRIVATE_H_
#define _VFSMAN_VFS_BACKEND_PRIVATE_H_

#include <vfsman/vfs.backend.h>

FERRO_STRUCT(vfsman_mount) {
	void* context;
	const vfsman_backend_t* backend;
	uint64_t open_descriptor_count;

	size_t path_length;
	char path[];
};

#endif // _VFSMAN_VFS_BACKEND_PRIVATE_H_
