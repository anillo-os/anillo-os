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
 * AARCH64 implementations of architecture-specific components for paging subsystem.
 */

#ifndef _FERRO_CORE_AARCH64_PAGING_H_
#define _FERRO_CORE_AARCH64_PAGING_H_

#include <stdbool.h>
#include <stddef.h>

#include <ferro/base.h>

#include <ferro/core/paging.h>

FERRO_DECLARATIONS_BEGIN;

/**
 * @addtogroup Paging
 *
 * @{
 */

#define FARCH_PAGE_PRESENT_BIT                    (1ULL << 0)
// for l1 table
#define FARCH_PAGE_VALID_PAGE_BIT                 (1ULL << 1)
// for l2 and l3 tables
#define FARCH_PAGE_TABLE_POINTER_BIT              (1ULL << 1)
#define FARCH_PAGE_ATTRIBUTES_INDEX_BITS          (3ULL << 2)
#define FARCH_PAGE_NONSECURE_BIT                  (1ULL << 5)
#define FARCH_PAGE_ALLOW_UNPRIVILEGED_ACCESS_BIT  (1ULL << 6)
#define FARCH_PAGE_NO_WRITE_BIT                   (1ULL << 7)
// or bits 50 and 51 of the physical address when LPA is available
#define FARCH_PAGE_SHAREABILITY_BITS              (3ULL << 8)
#define FARCH_PAGE_ACCESS_BIT                     (1ULL << 10)
#define FARCH_PAGE_NOT_GLOBAL_BIT                 (1ULL << 11)
#define FARCH_PAGE_NO_TRANSLATION_BIT             (1ULL << 16)
#define FARCH_PAGE_BTI_GUARDED_BIT                (1ULL << 50)
#define FARCH_PAGE_DIRTY_BIT                      (1ULL << 51)
#define FARCH_PAGE_CONTIGUOUS_BIT                 (1ULL << 52)
#define FARCH_PAGE_PRIVILEGED_EXECUTE_NEVER_BIT   (1ULL << 53)
#define FARCH_PAGE_UNPRIVILEGED_EXECUTE_NEVER_BIT (1ULL << 54)

FERRO_ALWAYS_INLINE uintptr_t fpage_virtual_to_physical_early(uintptr_t virtual_address) {
	uintptr_t result = virtual_address;
	__asm__(
		"at s1e1r, %0\n"
		"mrs %0, par_el1\n"
		:
		"+r" (result)
	);
	return (result & (0xfffffffffULL << 12)) | (virtual_address & 0xfffULL);
};

FERRO_ALWAYS_INLINE void fpage_begin_new_mapping(void* l4_address, void* old_stack_bottom, void* new_stack_bottom) {
	const void* stack_pointer = NULL;
	uintptr_t stack_diff = 0;
	uint64_t tcr_el1 = 0;

	__asm__("mov %0, sp" : "=r" (stack_pointer));
	stack_pointer = (void*)fpage_virtual_to_physical_early((uintptr_t)stack_pointer);
	stack_diff = (uintptr_t)old_stack_bottom - (uintptr_t)stack_pointer;

	__asm__ volatile(
		// make sure ttbr1_el1 is enabled/usable by clearing epd1
		"mrs %0, tcr_el1\n"
		// '\043' == '#'
		"bic %0, %0, \0430x800000\n"
		"msr tcr_el1, %0\n"

		"dsb sy\n"

		// load the new page table
		"msr ttbr1_el1, %1\n"

		// ensure the new page table is seen and used
		"dc civac, %1\n"
		"tlbi vmalle1\n"
		"isb sy\n"

		// load the new frame pointer
		"mov fp, %2\n"

		// load the new stack pointer
		"mov sp, %3\n"
		:
		"+r" (tcr_el1)
		:
		"r" (l4_address),
		"r" (new_stack_bottom),
		"r" ((uintptr_t)new_stack_bottom - stack_diff)
		:
		"memory"
	);
};

FERRO_ALWAYS_INLINE uint64_t fpage_page_entry(uintptr_t physical_address, bool writable) {
	return FARCH_PAGE_PRESENT_BIT | FARCH_PAGE_VALID_PAGE_BIT | FARCH_PAGE_ACCESS_BIT | (writable ? 0 : FARCH_PAGE_NO_WRITE_BIT) | (3ULL << 8) | (3ULL << 2) | (physical_address & (0xfffffffffULL << 12));
};

FERRO_ALWAYS_INLINE uint64_t fpage_large_page_entry(uintptr_t physical_address, bool writable) {
	return FARCH_PAGE_PRESENT_BIT | (writable ? 0 : FARCH_PAGE_NO_WRITE_BIT) | FARCH_PAGE_ACCESS_BIT | (3ULL << 8) | (3ULL << 2) | (physical_address & (0x7ffffffULL << 21));
};

FERRO_ALWAYS_INLINE uint64_t fpage_very_large_page_entry(uintptr_t physical_address, bool writable) {
	return FARCH_PAGE_PRESENT_BIT | (writable ? 0 : FARCH_PAGE_NO_WRITE_BIT) | FARCH_PAGE_ACCESS_BIT | (3ULL << 8) | (3ULL << 2) | (physical_address & (0xffffcULL << 30));
};

FERRO_ALWAYS_INLINE uint64_t fpage_table_entry(uintptr_t physical_address, bool writable) {
	// FARCH_PAGE_ACCESS_BIT is normally ignored for table entries, but for recursive entries, it's treated like the access bit for page entries
	return FARCH_PAGE_PRESENT_BIT | FARCH_PAGE_TABLE_POINTER_BIT | FARCH_PAGE_ACCESS_BIT | (writable ? 0 : FARCH_PAGE_NO_WRITE_BIT) | (physical_address & (0xfffffffffULL << 12));
};

FERRO_ALWAYS_INLINE bool fpage_entry_is_active(uint64_t entry_value) {
	return entry_value & FARCH_PAGE_PRESENT_BIT;
};

FERRO_ALWAYS_INLINE void fpage_invalidate_tlb_for_address(void* address) {
	uintptr_t input = (uintptr_t)address;
	input >>= 12;
	input &= 0xfffffffffff;
	__asm__ volatile("tlbi vale1is, %0" :: "r" (input) : "memory");
};

FERRO_ALWAYS_INLINE void fpage_synchronize_after_table_modification(void) {
	__asm__ volatile("dsb sy" ::: "memory");
};

FERRO_ALWAYS_INLINE bool fpage_entry_is_large_page_entry(uint64_t entry) {
	return !(entry & FARCH_PAGE_TABLE_POINTER_BIT);
};

FERRO_ALWAYS_INLINE uint64_t fpage_entry_disable_caching(uint64_t entry) {
	// TODO
	return entry;
};

/**
 * @}
 */

FERRO_DECLARATIONS_END;

#define USE_GENERIC_FPAGE_INVALIDATE_TLB_FOR_RANGE 1
#include <ferro/core/generic/paging.h>

#endif // _FERRO_CORE_AARCH64_PAGING_H_
