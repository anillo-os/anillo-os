/*
 * This file is part of Anillo OS
 * Copyright (C) 2023 Anillo OS Developers
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

#ifndef _LIBSYS_DATA_H_
#define _LIBSYS_DATA_H_

#include <stddef.h>
#include <stdint.h>

#include <libsys/base.h>
#include <libsys/objects.h>

LIBSYS_DECLARATIONS_BEGIN;

LIBSYS_OBJECT_CLASS(data);

LIBSYS_ENUM(uint64_t, sys_data_create_flags) {
	/**
	 * Create the data in shareable memory.
	 *
	 * This can be used e.g. to avoid data being copied when sent in a channel message.
	 *
	 * However, sometimes it's faster to copy small buffers than it is to
	 * setup shared memory, so don't optimize prematurely.
	 * TODO: I know this is true, but I'm not sure what the limit is.
	 */
	sys_data_create_flag_shared = 1 << 0,
};

LIBSYS_WUR ferr_t sys_data_create(const void* data, size_t length, sys_data_create_flags_t flags, sys_data_t** out_data);
LIBSYS_WUR ferr_t sys_data_create_nocopy(void* data, size_t length, sys_data_t** out_data);
LIBSYS_WUR ferr_t sys_data_create_transfer(void* data, size_t length, sys_data_t** out_data);
LIBSYS_WUR ferr_t sys_data_copy(sys_data_t* data, sys_data_t** out_data);
void* sys_data_contents(sys_data_t* data);
size_t sys_data_length(sys_data_t* data);

LIBSYS_DECLARATIONS_END;

#endif // _LIBSYS_DATA_H_
