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
#include <ferro/core/paging.h>

ferr_t fsyscall_handler_page_translate(void const* address, uint64_t* out_phys_address) {
	ferr_t status = ferr_ok;
	uintptr_t phys = fpage_space_virtual_to_physical(fpage_space_current(), (uintptr_t)address);
	if (phys == UINTPTR_MAX) {
		status = ferr_no_such_resource;
	}
	if (out_phys_address) {
		*out_phys_address = phys;
	}
out:
	return status;
};
