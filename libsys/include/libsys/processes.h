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

#ifndef _LIBSYS_PROCESSES_H_
#define _LIBSYS_PROCESSES_H_

#include <stdint.h>
#include <stddef.h>

#include <libsys/base.h>
#include <libsys/objects.h>
#include <libsys/files.h>

#include <ferro/error.h>

LIBSYS_DECLARATIONS_BEGIN;

LIBSYS_OBJECT_CLASS(sys_proc);

typedef uint64_t sys_proc_id_t;

#define SYS_PROC_ID_INVALID UINT64_MAX

LIBSYS_OPTIONS(uint64_t, sys_proc_flags) {
	/**
	 * Immediately start the process running upon successful creation.
	 */
	sys_proc_flag_resume = 1ULL << 0,

	/**
	 * Immediately detach the process upon successful creation.
	 */
	sys_proc_flag_detach = 1ULL << 1,
};

LIBSYS_WUR ferr_t sys_proc_create(sys_file_t* file, void* context_block, size_t context_block_size, sys_proc_flags_t flags, sys_proc_t** out_proc);
LIBSYS_WUR ferr_t sys_proc_resume(sys_proc_t* proc);
LIBSYS_WUR ferr_t sys_proc_suspend(sys_proc_t* proc);
sys_proc_t* sys_proc_current(void);
LIBSYS_WUR ferr_t sys_proc_detach(sys_proc_t* proc);

sys_proc_id_t sys_proc_id(sys_proc_t* proc);

LIBSYS_DECLARATIONS_END;

#endif // _LIBSYS_PROCESSES_H_
