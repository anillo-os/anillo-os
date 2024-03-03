/*
 * This file is part of Anillo OS
 * Copyright (C) 2024 Anillo OS Developers
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

ferr_t sys_page_allocate(size_t page_count, sys_page_flags_t flags, void** out_address) {
	return ferr_unsupported;
};

ferr_t sys_page_allocate_advanced(size_t page_count, uint8_t alignment_power, sys_page_flags_t flags, void** out_address) {
	return ferr_unsupported;
};

ferr_t sys_page_free(void* address) {
	return ferr_unsupported;
};

ferr_t sys_page_translate(const void* address, uint64_t* out_physical_address) {
	return ferr_unsupported;
};

ferr_t sys_shared_memory_allocate(size_t page_count, sys_shared_memory_flags_t flags, sys_shared_memory_t** out_shared_memory) {
	return ferr_unsupported;
};

ferr_t sys_shared_memory_map(sys_shared_memory_t* shared_memory, size_t page_count, size_t page_offset_count, void** out_address) {
	return ferr_unsupported;
};

ferr_t sys_shared_memory_bind(sys_shared_memory_t* shared_memory, size_t page_count, size_t page_offset_count, void* address) {
	return ferr_unsupported;
};

ferr_t sys_shared_memory_page_count(sys_shared_memory_t* shared_memory, size_t* out_page_count) {
	return ferr_unsupported;
};

