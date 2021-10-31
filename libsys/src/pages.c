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

#include <libsys/pages.h>
#include <gen/libsyscall/syscall-wrappers.h>

ferr_t sys_page_allocate(size_t page_count, uint64_t flags, void** out_address) {
	return libsyscall_wrapper_page_allocate_any(page_count, flags, out_address);
};

ferr_t sys_page_free(void* address) {
	return libsyscall_wrapper_page_free(address);
};
