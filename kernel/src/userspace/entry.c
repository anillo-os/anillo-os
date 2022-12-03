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

#include <ferro/userspace/entry.h>
#include <ferro/core/paging.h>
#include <ferro/userspace/threads.h>
#include <ferro/core/panic.h>
#include <libsimple/libsimple.h>
#include <ferro/core/scheduler.h>
#include <ferro/userspace/processes.h>
#include <ferro/syscalls/syscalls.h>
#include <ferro/core/console.h>
#include <ferro/userspace/process-registry.h>

void ferro_userspace_entry(void) {
	futhread_init();

	fsyscall_init();

	fprocreg_init();

	fconsole_log("Loading init process...\n");

	fvfs_descriptor_t* vfsman_desc = NULL;
	fpanic_status(fvfs_open("/sys/vfsman/vfsman", fvfs_descriptor_flag_read | fvfs_descriptor_flags_execute, &vfsman_desc));

	fproc_t* proc = NULL;
	fpanic_status(fproc_new(vfsman_desc, NULL, &proc));

	fpanic_status(fprocreg_register(proc));

	fpanic_status(fproc_resume(proc));

	fvfs_release(vfsman_desc);
	fproc_release(proc);
};
