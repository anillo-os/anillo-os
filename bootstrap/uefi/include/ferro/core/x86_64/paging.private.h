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
 * x86_64 implementations of architecture-specific private components for the paging subsystem.
 */

#ifndef _FERRO_CORE_X86_64_PAGING_PRIVATE_H_
#define _FERRO_CORE_X86_64_PAGING_PRIVATE_H_

#include <ferro/core/paging.private.h>

FERRO_DECLARATIONS_BEGIN;

#define FARCH_PAGE_PRESENT_BIT        (1ULL << 0)
#define FARCH_PAGE_WRITABLE_BIT       (1ULL << 1)
#define FARCH_PAGE_USER_BIT           (1ULL << 2)
#define FARCH_PAGE_WRITE_THROUGH_BIT  (1ULL << 3)
#define FARCH_PAGE_NO_CACHE_BIT       (1ULL << 4)
#define FARCH_PAGE_ACCESSED_BIT       (1ULL << 5)
#define FARCH_PAGE_DIRTY_BIT          (1ULL << 6)
#define FARCH_PAGE_HUGE_BIT           (1ULL << 7)
#define FARCH_PAGE_GLOBAL_BIT         (1ULL << 8)
#define FARCH_PAGE_NX_BIT             (1ULL << 63)

#define FARCH_PAGE_PHYS_ENTRY(x) ((uintptr_t)(x) & (0xffffffffff << 12))

#define FARCH_PAGE_VIRT_L3_HUGE_MASK 0x000000003fffffffULL
#define FARCH_PAGE_VIRT_L2_HUGE_MASK 0x00000000001fffffULL

FERRO_ALWAYS_INLINE uintptr_t fpage_virtual_to_physical_early(uintptr_t virtual_address) {
	fpage_table_t* l4;
	fpage_table_t* l3;
	fpage_table_t* l2;
	fpage_table_t* l1;
	uint64_t entry;

	__asm__("mov %%cr3, %0" : "=r" (l4));
	l4 = (fpage_table_t*)(((uintptr_t)l4) & 0xfffffffffffff000ULL);
	l3 = (fpage_table_t*)FARCH_PAGE_PHYS_ENTRY(l4->entries[FPAGE_VIRT_L4(virtual_address)]);

	entry = l3->entries[FPAGE_VIRT_L3(virtual_address)];
	if (entry & FARCH_PAGE_HUGE_BIT) {
		return FARCH_PAGE_PHYS_ENTRY(entry) | (virtual_address & FARCH_PAGE_VIRT_L3_HUGE_MASK);
	} else {
		l2 = (fpage_table_t*)FARCH_PAGE_PHYS_ENTRY(entry);
	}

	entry = l2->entries[FPAGE_VIRT_L2(virtual_address)];
	if (entry & FARCH_PAGE_HUGE_BIT) {
		return FARCH_PAGE_PHYS_ENTRY(entry) | (virtual_address & FARCH_PAGE_VIRT_L2_HUGE_MASK);
	} else {
		l1 = (fpage_table_t*)FARCH_PAGE_PHYS_ENTRY(entry);
	}

	return FARCH_PAGE_PHYS_ENTRY(l1->entries[FPAGE_VIRT_L1(virtual_address)]) | FPAGE_VIRT_OFFSET(virtual_address);
};

FERRO_ALWAYS_INLINE void fpage_begin_new_mapping(void* l4_address, void* old_stack_bottom, void* new_stack_bottom) {
	uintptr_t difference = new_stack_bottom - old_stack_bottom;
	__asm__ volatile(
		"mov %0, %%cr3\n"
		"addq %1, %%rbp\n"
		"addq %1, %%rsp\n"
		::
		"r" (l4_address),
		"r" (difference)
		:
		"memory"
	);
};

FERRO_ALWAYS_INLINE uint64_t fpage_page_entry(uintptr_t physical_address, bool writable) {
	return FARCH_PAGE_PRESENT_BIT | (writable ? FARCH_PAGE_WRITABLE_BIT : 0) | FARCH_PAGE_PHYS_ENTRY(physical_address);
};

FERRO_ALWAYS_INLINE uint64_t fpage_large_page_entry(uintptr_t physical_address, bool writable) {
	return FARCH_PAGE_PRESENT_BIT | (writable ? FARCH_PAGE_WRITABLE_BIT : 0) | FARCH_PAGE_HUGE_BIT | FARCH_PAGE_PHYS_ENTRY(physical_address);
};

FERRO_ALWAYS_INLINE uint64_t fpage_very_large_page_entry(uintptr_t physical_address, bool writable) {
	return FARCH_PAGE_PRESENT_BIT | (writable ? FARCH_PAGE_WRITABLE_BIT : 0) | FARCH_PAGE_HUGE_BIT | FARCH_PAGE_PHYS_ENTRY(physical_address);
};

FERRO_ALWAYS_INLINE uint64_t fpage_table_entry(uintptr_t physical_address, bool writable) {
	return FARCH_PAGE_PRESENT_BIT | (writable ? FARCH_PAGE_WRITABLE_BIT : 0) | FARCH_PAGE_PHYS_ENTRY(physical_address);
};

FERRO_ALWAYS_INLINE bool fpage_entry_is_active(uint64_t entry_value) {
	return entry_value & FARCH_PAGE_PRESENT_BIT;
};

FERRO_ALWAYS_INLINE void fpage_synchronize_after_table_modification(void) {
	// this isn't necessary on x86_64
};

FERRO_ALWAYS_INLINE bool fpage_entry_is_large_page_entry(uint64_t entry) {
	return entry & FARCH_PAGE_HUGE_BIT;
};

FERRO_ALWAYS_INLINE uint64_t fpage_entry_disable_caching(uint64_t entry) {
	return entry | FARCH_PAGE_NO_CACHE_BIT;
};

FERRO_ALWAYS_INLINE uintptr_t fpage_entry_address(uint64_t entry) {
	return FARCH_PAGE_PHYS_ENTRY(entry);
};

FERRO_ALWAYS_INLINE uint64_t fpage_entry_mark_active(uint64_t entry, bool active) {
	return (entry & ~FARCH_PAGE_PRESENT_BIT) | (active ? FARCH_PAGE_PRESENT_BIT : 0);
};

FERRO_ALWAYS_INLINE uint64_t fpage_entry_mark_privileged(uint64_t entry, bool privileged) {
	return (entry & ~FARCH_PAGE_USER_BIT) | (privileged ? 0 : FARCH_PAGE_USER_BIT);
};

FERRO_DECLARATIONS_END;

#endif // _FERRO_CORE_X86_64_PAGING_PRIVATE_H_