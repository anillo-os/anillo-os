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

/**
 * @file
 *
 * aarch64 implementations of architecture-specific private components for the paging subsystem.
 */

#ifndef _FERRO_CORE_AARCH64_PAGING_PRIVATE_H_
#define _FERRO_CORE_AARCH64_PAGING_PRIVATE_H_

#include <ferro/core/paging.private.h>
#include <ferro/core/interrupts.h>

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

	__asm__ volatile("mrs %0, tcr_el1" : "=r"(tcr_el1));

	// clear EPD1
	tcr_el1 &= ~(1ull << 23);

	// set TG{0,1}SZ to 16
	tcr_el1 = (tcr_el1 & ~(0x3full << 16)) | (16ull << 16);
	tcr_el1 = (tcr_el1 & ~(0x3full <<  0)) | (16ull << 0);

	// set TG{0,1} to 4KiB
	tcr_el1 = (tcr_el1 & ~(3ull << 30)) | (2ull << 30);
	tcr_el1 = (tcr_el1 & ~(3ull << 14)) | (0ull << 14);

	__asm__ volatile(
		"msr tcr_el1, %0\n"

		"dsb sy\n"

		// load the new page table
		// TODO: use two separate tables for each TTBR
		"msr ttbr0_el1, %1\n"
		"msr ttbr1_el1, %1\n"

		// ensure the new page table is seen and used
		"dc civac, %1\n"
		"tlbi vmalle1\n"
		"isb sy\n"

		// load the new frame pointer
		"mov fp, %2\n"

		// load the new stack pointer
		"mov sp, %3\n"
		::
		"r" (tcr_el1),
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
	return FARCH_PAGE_PRESENT_BIT | (writable ? 0 : FARCH_PAGE_NO_WRITE_BIT) | FARCH_PAGE_ACCESS_BIT | (3ULL << 8) | (3ULL << 2) | (physical_address & (0x3ffffULL << 30));
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

FERRO_ALWAYS_INLINE uintptr_t fpage_entry_address(uint64_t entry) {
	return entry & (0xfffffffffULL << 12);
};

FERRO_ALWAYS_INLINE uint64_t fpage_entry_mark_active(uint64_t entry, bool active) {
	return (entry & ~FARCH_PAGE_PRESENT_BIT) | (active ? FARCH_PAGE_PRESENT_BIT : 0);
};

FERRO_ALWAYS_INLINE uint64_t fpage_entry_mark_privileged(uint64_t entry, bool privileged) {
	return (entry & ~FARCH_PAGE_ALLOW_UNPRIVILEGED_ACCESS_BIT) | (privileged ? 0 : FARCH_PAGE_ALLOW_UNPRIVILEGED_ACCESS_BIT);
};

FERRO_ALWAYS_INLINE uintptr_t fpage_fault_address(void) {
	fint_frame_t* frame = fint_current_frame();
	if (!frame) {
		return UINTPTR_MAX;
	}
	return frame->far;
};

FERRO_ALWAYS_INLINE void fpage_invalidate_tlb_for_active_space(void) {
	__asm__ volatile(
		"tlbi vmalle1\n"
		"isb sy\n"
		:::
		"memory"
	);
};

FERRO_ALWAYS_INLINE void fpage_prefault_stack(size_t page_count) {
	extern bool fpage_prefaulting_enabled;
	if (!fpage_prefaulting_enabled) {
		return;
	}
	uint64_t sp;
	volatile uint8_t* ptr;
	__asm__ volatile("mov %0, sp" : "=r" (sp));
	ptr = (volatile void*)fpage_round_down_page(sp);
	for (size_t i = 0; i < page_count; ++i) {
		*(ptr - i * FPAGE_PAGE_SIZE);
	}
};

#define fpage_space_current_pointer() (&FARCH_PER_CPU(address_space))

/**
 * @}
 */

FERRO_DECLARATIONS_END;

#define USE_GENERIC_FPAGE_INVALIDATE_TLB_FOR_RANGE 1
#include <ferro/core/generic/paging.private.h>

#endif // _FERRO_CORE_AARCH64_PAGING_PRIVATE_H_
