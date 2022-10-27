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
#include <ferro/userspace/process-registry.h>

ferr_t fsyscall_handler_process_create(uint64_t fd, void const* context_block, uint64_t context_block_size, uint64_t* out_process_id) {
	ferr_t status = ferr_ok;
	fproc_t* proc = NULL;
	fvfs_descriptor_t* descriptor = NULL;
	const fproc_descriptor_class_t* desc_class = NULL;

	if (fproc_lookup_descriptor(fproc_current(), fd, true, (void*)&descriptor, &desc_class) != ferr_ok) {
		status = ferr_invalid_argument;
		goto out;
	}

	if (desc_class != &fproc_descriptor_class_vfs) {
		status = ferr_invalid_argument;
		goto out;
	}

	status = fproc_new(descriptor, fproc_current(), &proc);
	if (status != ferr_ok) {
		goto out;
	}

	status = fprocreg_register(proc);
	if (status != ferr_ok) {
		status = ferr_temporary_outage;
		fproc_kill(proc);
		goto out;
	}

	// TODO: use `context_block`

	if (out_process_id) {
		*out_process_id = proc->id;
	}

out:
	if (proc) {
		fproc_release(proc);
	}
	if (descriptor) {
		fvfs_release(descriptor);
	}
	return status;
};
