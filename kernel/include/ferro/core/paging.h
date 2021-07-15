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
#include <stdbool.h>

#include <ferro/base.h>
#include <ferro/platform.h>

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

extern char kernel_base_virtual;
extern char kernel_base_physical;

// these are arch-dependent functions we expect all architectures to implement

/**
 * Creates a 4KiB page table entry with the given information.
 */
FERRO_ALWAYS_INLINE uint64_t fpage_page_entry(uintptr_t physical_address, bool writable);

/**
 * Creates a 2MiB page table entry with the given information.
 */
FERRO_ALWAYS_INLINE uint64_t fpage_large_page_entry(uintptr_t physical_address, bool writable);

/**
 * Creates a page table entry to point to another page table.
 */
FERRO_ALWAYS_INLINE uint64_t fpage_table_entry(uintptr_t physical_address, bool writable);

/**
 * Jumps into a new virtual memory mapping using the given base table address and stack address.
 */
FERRO_ALWAYS_INLINE void fpage_begin_new_mapping(void* l4_address, void* old_stack_bottom, void* new_stack_bottom);

/**
 * Translate the given virtual address into a physical address.
 */
FERRO_ALWAYS_INLINE uintptr_t fpage_virtual_to_physical(uintptr_t virtual_address)

FERRO_DECLARATIONS_END;

// now include the arch-dependent header
#if FERRO_ARCH == FERRO_ARCH_x86_64
	#include <ferro/core/x86_64/paging.h>
#elif FERRO_ARCH == FERRO_ARCH_aarch64
	#include <ferro/core/aarch64/paging.h>
#else
	#error Unrecognized/unsupported CPU architecture! (see <ferro/core/paging.h>)
#endif

#endif // _FERRO_CORE_PAGING_H_
