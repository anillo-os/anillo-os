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

/**
 * @file
 *
 * Generic implementations of architecture-specific components for the paging subsystem.
 */

#ifndef _FERRO_CORE_GENERIC_PAGING_PRIVATE_H_
#define _FERRO_CORE_GENERIC_PAGING_PRIVATE_H_

#include <ferro/base.h>

#include <ferro/core/paging.private.h>

FERRO_DECLARATIONS_BEGIN;

/**
 * @addtogroup Paging
 *
 * @{
 */

/**
 * @cond internal
 *
 * Generic (and inefficient) implementation of fpage_invalidate_tlb_for_range_all_cpus() that uses fpage_invalidate_tlb_for_address_all_cpus().
 */
FERRO_ALWAYS_INLINE void generic_fpage_invalidate_tlb_for_range(void* start, void* end) {
	uintptr_t start_addr = (uintptr_t)start;
	uintptr_t end_addr = (uintptr_t)end;
	if (end_addr - start_addr > FPAGE_PAGE_SIZE) {
		// it's faster to just invalidate all entries
		fpage_invalidate_tlb_for_active_space();
	}

	fpage_invalidate_tlb_for_address((void*)start_addr);
};

FERRO_ALWAYS_INLINE void generic_fpage_invalidate_tlb_for_range_all_cpus(void* start, void* end) {
	uintptr_t start_addr = (uintptr_t)start;
	uintptr_t end_addr = (uintptr_t)end;
	if (end_addr - start_addr > FPAGE_PAGE_SIZE) {
		// it's faster to just invalidate all entries
		fpage_invalidate_tlb_for_active_space_all_cpus();
	}

	fpage_invalidate_tlb_for_address_all_cpus((void*)start_addr);
};

#if USE_GENERIC_FPAGE_INVALIDATE_TLB_FOR_RANGE
	FERRO_ALWAYS_INLINE void fpage_invalidate_tlb_for_range(void* start, void* end) {
		return generic_fpage_invalidate_tlb_for_range(start, end);
	};

	FERRO_ALWAYS_INLINE void fpage_invalidate_tlb_for_range_all_cpus(void* start, void* end) {
		return generic_fpage_invalidate_tlb_for_range_all_cpus(start, end);
	};
#endif

/**
 * @}
 */

FERRO_DECLARATIONS_END;

#endif // _FERRO_CORE_GENERIC_PAGING_PRIVATE_H_
