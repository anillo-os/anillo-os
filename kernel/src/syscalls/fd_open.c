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

ferr_t fsyscall_handler_fd_open(const char* path, uint64_t path_length, uint64_t flags, uint64_t* out_fd) {
	ferr_t status = ferr_ok;
	fvfs_descriptor_t* desc = NULL;

	status = fvfs_open_n(path, path_length, flags, &desc);
	if (status != ferr_ok) {
		return status;
	}

	status = fproc_install_descriptor(fproc_current(), desc, &fproc_descriptor_class_vfs, out_fd);
	fvfs_release(desc);

	return status;
};
