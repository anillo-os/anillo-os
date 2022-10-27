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

#ifndef _LIBSYS_PAGES_H_
#define _LIBSYS_PAGES_H_

#include <stddef.h>
#include <stdint.h>

#include <libsys/base.h>
#include <ferro/error.h>
#include <libsys/objects.h>

LIBSYS_DECLARATIONS_BEGIN;

LIBSYS_OBJECT_CLASS(shared_memory);

LIBSYS_OPTIONS(uint64_t, sys_shared_memory_flags) {
	sys_shared_memory_flag_xxx_reserved = 0
};

LIBSYS_OPTIONS(uint64_t, sys_page_flags) {
	sys_page_flag_contiguous  = 1 << 0,
	sys_page_flag_prebound    = 1 << 1,
	sys_page_flag_unswappable = 1 << 2,
	sys_page_flag_uncacheable = 1 << 3,
};

LIBSYS_WUR ferr_t sys_page_allocate(size_t page_count, sys_page_flags_t flags, void** out_address);
LIBSYS_WUR ferr_t sys_page_allocate_advanced(size_t page_count, uint8_t alignment_power, sys_page_flags_t flags, void** out_address);

LIBSYS_WUR ferr_t sys_page_free(void* address);

LIBSYS_WUR ferr_t sys_page_translate(const void* address, uint64_t* out_physical_address);

LIBSYS_WUR ferr_t sys_shared_memory_allocate(size_t page_count, sys_shared_memory_flags_t flags, sys_shared_memory_t** out_shared_memory);
LIBSYS_WUR ferr_t sys_shared_memory_map(sys_shared_memory_t* shared_memory, size_t page_count, size_t page_offset_count, void** out_address);
LIBSYS_WUR ferr_t sys_shared_memory_bind(sys_shared_memory_t* shared_memory, size_t page_count, size_t page_offset_count, void* address);

LIBSYS_ALWAYS_INLINE uintptr_t sys_page_round_up_multiple(uintptr_t number) {
	return (number + (4096ULL - 1)) & -4096ULL;
};

LIBSYS_ALWAYS_INLINE uintptr_t sys_page_round_up_count(uintptr_t number) {
	return sys_page_round_up_multiple(number) / 4096ULL;
};

LIBSYS_ALWAYS_INLINE uintptr_t sys_page_round_down_multiple(uintptr_t number) {
	return number & -4096ULL;
};

LIBSYS_ALWAYS_INLINE uintptr_t sys_page_round_down_count(uintptr_t number) {
	return number / 4096ULL;
};

LIBSYS_DECLARATIONS_END;

#endif // _LIBSYS_PAGES_H_
