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

ferr_t fsyscall_handler_fd_read(uint64_t fd, uint64_t offset, uint64_t desired_length, void* out_buffer, void* out_read_length) {
	fvfs_descriptor_t* descriptor = NULL;
	ferr_t status = ferr_ok;

	if (fproc_lookup_descriptor(fproc_current(), fd, true, &descriptor) != ferr_ok) {
		status = ferr_invalid_argument;
		goto out;
	}

	status = fvfs_read(descriptor, offset, out_buffer, desired_length, out_read_length);
	if (status != ferr_ok) {
		goto out;
	}

out:
	if (descriptor != NULL) {
		fvfs_release(descriptor);
	}
	return status;
};
