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

#ifndef _FERRO_CORE_PAGING_H_
#define _FERRO_CORE_PAGING_H_

#include <stdint.h>

#include <ferro/base.h>

FERRO_DECLARATIONS_BEGIN;

#define FERRO_KERNEL_VIRTUAL_START  ((uintptr_t)&kernel_base_virtual)
#define FERRO_KERNEL_PHYSICAL_START ((uintptr_t)&kernel_base_physical)

#define FERRO_KERNEL_VIRT_TO_PHYS(x) (((uintptr_t)x - FERRO_KERNEL_VIRTUAL_START))

#define FERRO_PAGE_ALIGNED __attribute__((aligned(4096)))

#define FPAGE_PAGE_SIZE       0x001000ULL
#define FPAGE_LARGE_PAGE_SIZE 0x200000ULL

#define FPAGE_VIRT_L1_SHIFT 12
#define FPAGE_VIRT_L2_SHIFT 21
#define FPAGE_VIRT_L3_SHIFT 30
#define FPAGE_VIRT_L4_SHIFT 39

#define FPAGE_VIRT_L3_HUGE_MASK 0x000000003fffffffULL
#define FPAGE_VIRT_L2_HUGE_MASK 0x00000000001fffffULL

#define FPAGE_VIRT_OFFSET(x) ((uintptr_t)(x) & 0xfffULL)
#define FPAGE_VIRT_L1(x)     (((uintptr_t)(x) & (0x1ffULL << FPAGE_VIRT_L1_SHIFT)) >> FPAGE_VIRT_L1_SHIFT)
#define FPAGE_VIRT_L2(x)     (((uintptr_t)(x) & (0x1ffULL << FPAGE_VIRT_L2_SHIFT)) >> FPAGE_VIRT_L2_SHIFT)
#define FPAGE_VIRT_L3(x)     (((uintptr_t)(x) & (0x1ffULL << FPAGE_VIRT_L3_SHIFT)) >> FPAGE_VIRT_L3_SHIFT)
#define FPAGE_VIRT_L4(x)     (((uintptr_t)(x) & (0x1ffULL << FPAGE_VIRT_L4_SHIFT)) >> FPAGE_VIRT_L4_SHIFT)

#define FPAGE_BUILD_VIRT(l4, l3, l2, l1, offset) ((0xffffULL << 48) | (((uintptr_t)(l4) & 0x1ffULL) << FPAGE_VIRT_L4_SHIFT) | (((uintptr_t)(l3) & 0x1ffULL) << FPAGE_VIRT_L3_SHIFT) | (((uintptr_t)(l2) & 0x1ffULL) << FPAGE_VIRT_L2_SHIFT) | (((uintptr_t)(l1) & 0x1ffULL) << FPAGE_VIRT_L1_SHIFT) | ((uintptr_t)(offset) & 0xfffULL))

typedef struct fpage_table fpage_table_t;
struct fpage_table {
	uint64_t entries[512];
};

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
#define FPAGE_PHYS_ENTRY(x)       ((uintptr_t)(x) & (0xffffffffff << 12))

extern char kernel_base_virtual;
extern char kernel_base_physical;

extern char kernel_start_virtual;
extern char kernel_end_virtual;
extern char kernel_start_physical;

FERRO_INLINE uintptr_t fpage_virtual_to_physical(uintptr_t virtual_address) {
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

FERRO_DECLARATIONS_END;

#endif // _FERRO_CORE_PAGING_H_
