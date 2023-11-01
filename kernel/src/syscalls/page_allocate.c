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
#include <libsimple/libsimple.h>
#include <ferro/userspace/uio.h>

ferr_t fsyscall_handler_page_allocate(uint64_t page_count, fsyscall_page_allocate_flags_t flags, uint8_t alignment_power, void* xout_address) {
	ferr_t status = ferr_ok;
	void* address = NULL;
	void* phys_address = NULL;
	void** out_address = xout_address;
	fpage_flags_t page_flags = fpage_flag_unprivileged | fpage_flag_zero;
	bool unregister_mapping_on_fail = false;

	if (!out_address) {
		status = ferr_invalid_argument;
		goto out;
	}

	if (flags & fsyscall_page_allocate_flag_prebound) {
		page_flags |= fpage_flag_prebound;
	}

	if (flags & fsyscall_page_allocate_flag_uncacheable) {
		page_flags |= fpage_flag_no_cache;
	}

	if (flags & fsyscall_page_allocate_flag_contiguous) {
		if (fpage_allocate_physical_aligned(page_count, alignment_power, NULL, &phys_address, 0) != ferr_ok) {
			status = ferr_temporary_outage;
			goto out;
		}
		if (fpage_space_map_aligned(fpage_space_current(), phys_address, page_count, alignment_power, &address, page_flags) != ferr_ok) {
			status = ferr_temporary_outage;
			goto out;
		}
	} else {
		if (fpage_space_allocate_aligned(fpage_space_current(), page_count, alignment_power, &address, page_flags) != ferr_ok) {
			status = ferr_temporary_outage;
			goto out;
		}
	}

	status = fproc_register_mapping(fproc_current(), address, page_count, (flags & fsyscall_page_allocate_flag_contiguous) ? fproc_mapping_flag_contiguous : 0, NULL);
	if (status != ferr_ok) {
		goto out;
	}

	unregister_mapping_on_fail = true;

	status = ferro_uio_copy_out(&address, sizeof(address), (uintptr_t)out_address);

out:
	if (status != ferr_ok) {
		if (unregister_mapping_on_fail) {
			FERRO_WUR_IGNORE(fproc_unregister_mapping(fproc_current(), address, NULL, NULL, NULL));
		}
		if (address) {
			if (flags & fsyscall_page_allocate_flag_contiguous) {
				FERRO_WUR_IGNORE(fpage_space_unmap(fpage_space_current(), address, page_count));
			} else {
				FERRO_WUR_IGNORE(fpage_space_free(fpage_space_current(), address, page_count));
			}
		}
		if (phys_address) {
			FERRO_WUR_IGNORE(fpage_free_physical(phys_address, page_count));
		}
	}
	return status;
};
