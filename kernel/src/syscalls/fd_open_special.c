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
#include <ferro/core/vfs.backend.h>
#include <ferro/core/console.h>

static ferr_t console_stdout_write(void* context, fvfs_descriptor_t* descriptor, size_t offset, const void* buffer, size_t buffer_size, size_t* out_written_count);

static const fvfs_backend_t console_stdout_backend = {
	.write = console_stdout_write,
};

static ferr_t console_stdout_write(void* context, fvfs_descriptor_t* descriptor, size_t offset, const void* buffer, size_t buffer_size, size_t* out_written_count) {
	if (offset != 0) {
		return ferr_invalid_argument;
	}

	fconsole_logn(buffer, buffer_size);

	if (out_written_count) {
		*out_written_count = buffer_size;
	}

	return ferr_ok;
};

ferr_t fsyscall_handler_fd_open_special(uint64_t special_id, uint64_t* out_fd) {
	switch (special_id) {
		case 0:
			return fproc_install_descriptor(fproc_current(), fproc_current()->binary_descriptor, out_fd);

		case 1: {
			ferr_t status = ferr_ok;
			fvfs_descriptor_t* desc = NULL;

			status = fvfs_open_anonymous("console-stdout", SIZE_MAX, &console_stdout_backend, fproc_current(), &desc);
			if (status != ferr_ok) {
				return status;
			}

			status = fproc_install_descriptor(fproc_current(), desc, out_fd);
			if (status != ferr_ok) {
				fvfs_release(desc);
			}
			return status;
		} break;

		default:
			return ferr_no_such_resource;
	}
};
