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

ferr_t fsyscall_handler_page_free(void* address) {
	ferr_t status = ferr_ok;
	size_t page_count = 0;

	if (fproc_unregister_mapping(fproc_current(), address, &page_count) != ferr_ok) {
		status = ferr_no_such_resource;
		goto out;
	}

	FERRO_WUR_IGNORE(fpage_space_free(fpage_space_current(), address, page_count));

out:
	return status;
};
