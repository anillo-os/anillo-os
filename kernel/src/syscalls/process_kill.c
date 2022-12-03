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

#include <gen/ferro/userspace/syscall-handlers.h>
#include <ferro/userspace/process-registry.h>

extern const fproc_descriptor_class_t fsyscall_proc_class;

ferr_t fsyscall_handler_process_kill(uint64_t process_handle) {
	ferr_t status = ferr_ok;
	const fproc_descriptor_class_t* desc_class = NULL;
	fproc_t* proc = NULL;

	status = fproc_lookup_descriptor(fproc_current(), process_handle, true, (void*)&proc, &desc_class);
	if (status != ferr_ok) {
		goto out;
	}

	if (desc_class != &fsyscall_proc_class) {
		status = ferr_invalid_argument;
		goto out;
	}

	if (proc == fproc_current()) {
		// we're going to kill ourselves now;
		// let's not leak a reference on ourselves.
		// anyways, we're the current process, so we MUST remain valid
		// (the current thread holds a reference on us).
		fproc_release(proc);
	}

	status = fproc_kill(proc);

out:
	if (proc) {
		fproc_release(proc);
	}
	return status;
};
