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
#include <ferro/core/paging.h>

ferr_t fsyscall_handler_page_protect(const void* address, uint64_t page_count, fsyscall_page_permissions_t permissions) {
	ferr_t status = ferr_ok;
	size_t total_page_count = 0;
	fproc_mapping_flags_t flags = 0;

	if (fproc_lookup_mapping(fproc_current(), (void*)address, &total_page_count, &flags, NULL) != ferr_ok) {
		status = ferr_no_such_resource;
		goto out;
	}

	status = fpage_space_change_permissions(fpage_space_current(), (void*)address, page_count, permissions);

out:
	return status;
};
