/*
 * This file is part of Anillo OS
 * Copyright (C) 2020 Anillo OS Developers
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

#ifndef _FERRO_CORE_GENERIC_PAGING_H_
#define _FERRO_CORE_GENERIC_PAGING_H_

#include <ferro/base.h>

#include <ferro/core/paging.h>

FERRO_DECLARATIONS_BEGIN;

/**
 * Generic (and inefficient) implementation of `fpage_invalidate_tlb_for_range` that uses `fpage_invalidate_tlb_for_address`.
 */
FERRO_ALWAYS_INLINE void generic_fpage_invalidate_tlb_for_range(void* start, void* end) {
	uintptr_t start_addr = (uintptr_t)start;
	uintptr_t end_addr = (uintptr_t)end;
	for (uintptr_t addr = start_addr; addr < end_addr; addr += FPAGE_PAGE_SIZE) {
		fpage_invalidate_tlb_for_address((void*)addr);
	}
};

#if USE_GENERIC_FPAGE_INVALIDATE_TLB_FOR_RANGE
	FERRO_ALWAYS_INLINE void fpage_invalidate_tlb_for_range(void* start, void* end) {
		return generic_fpage_invalidate_tlb_for_range(start, end);
	};
#endif

FERRO_DECLARATIONS_END;

#endif // _FERRO_CORE_GENERIC_PAGING_H_
