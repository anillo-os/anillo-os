/**
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

#ifndef _FERRO_CORE_X86_64_PAGING_H_
#define _FERRO_CORE_X86_64_PAGING_H_

#include <stddef.h>
#include <stdint.h>

#include <ferro/base.h>

#include <ferro/core/paging.h>

FERRO_DECLARATIONS_BEGIN;

#define FPAGE_VIRT_L3_HUGE_MASK 0x000000003fffffffULL
#define FPAGE_VIRT_L2_HUGE_MASK 0x00000000001fffffULL

#define FPAGE_PRESENT_BIT        (1ULL << 0)
#define FPAGE_WRITABLE_BIT       (1ULL << 1)
#define FPAGE_USER_BIT           (1ULL << 2)
#define FPAGE_WRITE_THROUGH_BIT  (1ULL << 3)
#define FPAGE_NO_CACHE_BIT       (1ULL << 4)
#define FPAGE_ACCESSED_BIT       (1ULL << 5)
#define FPAGE_DIRTY_BIT          (1ULL << 6)
#define FPAGE_HUGE_BIT           (1ULL << 7)
#define FPAGE_GLOBAL_BIT         (1ULL << 8)
#define FPAGE_NX_BIT             (1ULL << 63)

#define FPAGE_PHYS_ENTRY(x) ((uintptr_t)(x) & (0xffffffffff << 12))

FERRO_ALWAYS_INLINE uintptr_t fpage_virtual_to_physical_early(uintptr_t virtual_address) {
	fpage_table_t* l4;
	fpage_table_t* l3;
	fpage_table_t* l2;
	fpage_table_t* l1;
	uint64_t entry;

	__asm__("mov %%cr3, %0" : "=r" (l4));
	l4 = (fpage_table_t*)(((uintptr_t)l4) & 0xfffffffffffff000ULL);
	l3 = (fpage_table_t*)FPAGE_PHYS_ENTRY(l4->entries[FPAGE_VIRT_L4(virtual_address)]);

	entry = l3->entries[FPAGE_VIRT_L3(virtual_address)];
	if (entry & FPAGE_HUGE_BIT) {
		return FPAGE_PHYS_ENTRY(entry) | (virtual_address & FPAGE_VIRT_L3_HUGE_MASK);
	} else {
		l2 = (fpage_table_t*)FPAGE_PHYS_ENTRY(entry);
	}

	entry = l2->entries[FPAGE_VIRT_L2(virtual_address)];
	if (entry & FPAGE_HUGE_BIT) {
		return FPAGE_PHYS_ENTRY(entry) | (virtual_address & FPAGE_VIRT_L2_HUGE_MASK);
	} else {
		l1 = (fpage_table_t*)FPAGE_PHYS_ENTRY(entry);
	}

	return FPAGE_PHYS_ENTRY(l1->entries[FPAGE_VIRT_L1(virtual_address)]) | FPAGE_VIRT_OFFSET(virtual_address);
};

FERRO_ALWAYS_INLINE void fpage_begin_new_mapping(void* l4_address, void* old_stack_bottom, void* new_stack_bottom) {
	const void* rsp = NULL;
	uintptr_t stack_diff = 0;

	__asm__("mov %%rsp, %0" : "=r" (rsp));
	rsp = (void*)fpage_virtual_to_physical_early((uintptr_t)rsp);
	stack_diff = (uintptr_t)old_stack_bottom - (uintptr_t)rsp;

	__asm__(
		"mov %0, %%cr3\n"
		"mov %1, %%rbp\n"
		"mov %2, %%rsp\n"
		::
		"r" (l4_address),
		"r" (new_stack_bottom),
		"r" ((uintptr_t)new_stack_bottom - stack_diff)
	);
};

FERRO_ALWAYS_INLINE uint64_t fpage_page_entry(uintptr_t physical_address, bool writable) {
	return FPAGE_PRESENT_BIT | (writable ? FPAGE_WRITABLE_BIT : 0) | FPAGE_PHYS_ENTRY(physical_address);
};

FERRO_ALWAYS_INLINE uint64_t fpage_large_page_entry(uintptr_t physical_address, bool writable) {
	return FPAGE_PRESENT_BIT | (writable ? FPAGE_WRITABLE_BIT : 0) | FPAGE_HUGE_BIT | FPAGE_PHYS_ENTRY(physical_address);
};

FERRO_ALWAYS_INLINE uint64_t fpage_very_large_page_entry(uintptr_t physical_address, bool writable) {
	return FPAGE_PRESENT_BIT | (writable ? FPAGE_WRITABLE_BIT : 0) | FPAGE_HUGE_BIT | FPAGE_PHYS_ENTRY(physical_address);
};

FERRO_ALWAYS_INLINE uint64_t fpage_table_entry(uintptr_t physical_address, bool writable) {
	return FPAGE_PRESENT_BIT | (writable ? FPAGE_WRITABLE_BIT : 0) | FPAGE_PHYS_ENTRY(physical_address);
};

FERRO_ALWAYS_INLINE void fpage_invalidate_tlb_for_address(void* address) {
	__asm__("invlpg %0" :: "m" (address));
};

FERRO_ALWAYS_INLINE bool fpage_entry_is_active(uint64_t entry_value) {
	return entry_value & FPAGE_PRESENT_BIT;
};

FERRO_ALWAYS_INLINE void fpage_synchronize_after_table_modification(void) {
	// this isn't necessary on x86_64
};

FERRO_ALWAYS_INLINE bool fpage_entry_is_large_page_entry(uint64_t entry) {
	return entry & FPAGE_HUGE_BIT;
};

FERRO_DECLARATIONS_END;

#define USE_GENERIC_FPAGE_INVALIDATE_TLB_FOR_RANGE 1
#include <ferro/core/generic/paging.h>

#endif // _FERRO_CORE_X86_64_PAGING_H_
