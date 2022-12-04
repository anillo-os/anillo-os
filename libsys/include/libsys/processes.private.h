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

#ifndef _LIBSYS_PROCESSES_PRIVATE_H_
#define _LIBSYS_PROCESSES_PRIVATE_H_

#include <libsys/base.h>
#include <libsys/processes.h>
#include <libsys/objects.private.h>
#include <libsys/locks.h>

#include <ferro/error.h>

LIBSYS_DECLARATIONS_BEGIN;

typedef uint64_t sys_proc_handle_t;

LIBSYS_STRUCT(sys_proc_object) {
	sys_object_t object;
	sys_proc_id_t id;
	sys_proc_handle_t handle;
	bool detached;
};

LIBSYS_WUR ferr_t sys_proc_init(void);

LIBSYS_DECLARATIONS_END;

#endif // _LIBSYS_PROCESSES_PRIVATE_H_
