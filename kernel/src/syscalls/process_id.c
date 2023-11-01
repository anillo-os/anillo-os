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
#include <ferro/userspace/processes.h>
#include <ferro/userspace/uio.h>

extern const fproc_descriptor_class_t fsyscall_proc_class;

// XXX: this should be in its own file. maybe.
ferr_t fsyscall_handler_process_current(uint64_t* out_process_handle) {
	uint64_t process_handle = FPROC_DID_MAX;
	ferr_t status = ferr_ok;

	status = fproc_install_descriptor(fproc_current(), fproc_current(), &fsyscall_proc_class, &process_handle);
	if (status != ferr_ok) {
		goto out;
	}

	status = ferro_uio_copy_out(&process_handle, sizeof(process_handle), (uintptr_t)out_process_handle);

out:
	if (status != ferr_ok) {
		if (process_handle != FPROC_DID_MAX) {
			FERRO_WUR_IGNORE(fproc_uninstall_descriptor(fproc_current(), process_handle));
		}
	}
	return status;
};

ferr_t fsyscall_handler_process_id(uint64_t process_handle, uint64_t* out_process_id) {
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

	status = ferro_uio_copy_out(&proc->id, sizeof(proc->id), (uintptr_t)out_process_id);

out:
	if (proc) {
		fproc_release(proc);
	}
	return status;
};
