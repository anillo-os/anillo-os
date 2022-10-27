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

#include <gen/ferro/userspace/syscall-handlers.h>
#include <ferro/userspace/processes.h>
#include <ferro/core/vfs.h>

ferr_t fsyscall_handler_fd_copy_path(uint64_t fd, uint64_t buffer_size, char* out_buffer, uint64_t* out_actual_size) {
	fvfs_descriptor_t* descriptor = NULL;
	ferr_t status = ferr_ok;
	const fproc_descriptor_class_t* desc_class = NULL;
	size_t actual_size = 0;

	if (fproc_lookup_descriptor(fproc_current(), fd, true, (void*)&descriptor, &desc_class) != ferr_ok) {
		status = ferr_invalid_argument;
		goto out;
	}

	if (desc_class != &fproc_descriptor_class_vfs) {
		status = ferr_invalid_argument;
		goto out;
	}

	status = fvfs_copy_path(descriptor, true, out_buffer, buffer_size, &actual_size);

	if (out_actual_size) {
		*out_actual_size = actual_size;
	}

	if (status != ferr_ok) {
		goto out;
	}

out:
	if (descriptor != NULL) {
		fvfs_release(descriptor);
	}
	return status;
};
