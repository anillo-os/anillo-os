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

ferr_t fsyscall_handler_page_allocate_any(uint64_t page_count, uint64_t flags, void* xout_address) {
	ferr_t status = ferr_ok;
	void* address = NULL;
	void** out_address = xout_address;

	if (!out_address) {
		status = ferr_invalid_argument;
		goto out;
	}

	// TODO: use flags

	if (fpage_space_allocate(fpage_space_current(), page_count, &address, fpage_flag_unprivileged) != ferr_ok) {
		status = ferr_temporary_outage;
		goto out;
	}

	status = fproc_register_mapping(fproc_current(), address, page_count);

out:
	if (status == ferr_ok) {
		*out_address = address;
	} else {
		if (address) {
			FERRO_WUR_IGNORE(fpage_space_free(fpage_space_current(), address, page_count));
		}
	}
	return status;
};
