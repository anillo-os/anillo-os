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

#ifndef _VFSMAN_LIBVFS_COMMON_H_
#define _VFSMAN_LIBVFS_COMMON_H_

#include <stdint.h>

#include <libvfs/base.h>

LIBVFS_DECLARATIONS_BEGIN;

FERRO_ENUM(uint8_t, vfsman_node_type) {
	vfsman_node_type_invalid = 0,
	vfsman_node_type_file,
	vfsman_node_type_directory,
};

LIBVFS_DECLARATIONS_END;

#endif // _VFSMAN_LIBVFS_COMMON_H_
