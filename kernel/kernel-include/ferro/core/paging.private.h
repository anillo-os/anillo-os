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
 * Paging subsystem; private components.
 */

#ifndef _FERRO_CORE_PAGING_PRIVATE_H_
#define _FERRO_CORE_PAGING_PRIVATE_H_

#include <ferro/core/paging.h>

FERRO_DECLARATIONS_BEGIN;

/**
 * @addtogroup Paging
 *
 * The paging subsystem.
 *
 * @{
 */

// free blocks are doubly-linked list nodes
FERRO_STRUCT(fpage_free_block) {
	fpage_free_block_t** prev;
	fpage_free_block_t* next;
};

#define FPAGE_MAX_ORDER 32

FERRO_STRUCT(fpage_region_header) {
	fpage_region_header_t** prev;
	fpage_region_header_t* next;
	size_t page_count;
	void* start;
	fpage_free_block_t* buckets[FPAGE_MAX_ORDER];

	// this lock protects the region and all of its blocks from reads and writes (for the bookkeeping info; not the actual content itself, obviously)
	flock_spin_intsafe_t lock;

	// if a bit is 0, the block corresponding to that bit is free.
	// if a bit is 1, the block corresponding to that bit is in use.
	uint8_t bitmap[];
};

FERRO_STRUCT(fpage_space) {
	/**
	 * The physical address of the L4 table containing all the mappings for this portion of the address space.
	 *
	 * The present entries in this table are loaded when the address space is activated and unloaded when the address space is deactivated.
	 */
	fpage_table_t* l4_table;

	/**
	 * The head of the list of page mappings in this address space.
	 *
	 * @todo Actually implement this. Right now, it gets initialized, but nothing ever gets added to it.
	 */
	fpage_mapping_t* head;

	flock_spin_intsafe_t regions_head_lock;

	/**
	 * The head of the list of buddy allocator regions for this address space.
	 */
	fpage_region_header_t* regions_head;

	bool active;

	flock_spin_intsafe_t allocation_lock;

	/**
	 * A waitq to wait for the address space to be destroyed.
	 *
	 * These waiters are notified right before any destruction is actually performed.
	 */
	fwaitq_t space_destruction_waiters;
};

/**
 * Calculates the recursive virtual address for accessing a page table.
 *
 * Examples:
 *   * `levels = 0` returns the virtual addres of the level 4 page table (a.k.a. `l4`).
 *   * `levels = 1, l4_index = 5` returns the virtual address of the level 3 page table at index 5 in the level 4 page table (a.k.a. `l4[5] -> l3`).
 *   * `levels = 2, l4_index = 5, l3_index = 2` returns the virtual address of the level 2 page table at `l4[5] -> l3, l3[2] -> l2`.
 *   * `levels = 3, l4_index = 5, l3_index = 2, l2_index = 18` returns the virtual address of the level 1 page table at `l4[5] -> l3, l3[2] -> l2, l2[18] -> l1`.
 *
 * @param levels   The number of non-recursive indicies to use. Must be between 0-3 (inclusive).
 * @param l4_index The non-recursive L4 index to use when `levels > 0`.
 * @param l3_index The non-recursive L3 index to use when `levels > 1`.
 * @param l2_index The non-recursive L2 index to use when `levels > 2`.
 */
uintptr_t fpage_virtual_address_for_table(size_t levels, uint16_t l4_index, uint16_t l3_index, uint16_t l2_index);

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
 * Creates a 1GiB page table entry with the given information.
 */
FERRO_ALWAYS_INLINE uint64_t fpage_very_large_page_entry(uintptr_t physical_address, bool writable);

/**
 * Creates a page table entry to point to another page table.
 */
FERRO_ALWAYS_INLINE uint64_t fpage_table_entry(uintptr_t physical_address, bool writable);

/**
 * Jumps into a new virtual memory mapping using the given base table address and stack address.
 */
FERRO_ALWAYS_INLINE void fpage_begin_new_mapping(void* l4_address, void* old_stack_bottom, void* new_stack_bottom);

/**
 * Translates the given virtual address into a physical address. Only valid during early startup.
 */
FERRO_ALWAYS_INLINE uintptr_t fpage_virtual_to_physical_early(uintptr_t virtual_address);

/**
 * Determines whether an entry with the given value is active or not.
 */
FERRO_ALWAYS_INLINE bool fpage_entry_is_active(uint64_t entry_value);

/**
 * Invalidates the TLB entry/entries for the given virtual address.
 */
FERRO_ALWAYS_INLINE void fpage_invalidate_tlb_for_address(void* address);

/**
 * On architectures where this is necessary, triggers a synchronization. This is meant to be called after any table modification.
 */
FERRO_ALWAYS_INLINE void fpage_synchronize_after_table_modification(void);

/**
 * Returns `true` if the given entry represents a large or very large page.
 */
FERRO_ALWAYS_INLINE bool fpage_entry_is_large_page_entry(uint64_t entry);

/**
 * Invalidates the TLB entry/entries for the given range of virtual addresses (non-inclusive).
 */
FERRO_ALWAYS_INLINE void fpage_invalidate_tlb_for_range(void* start, void* end);

/**
 * Creates a modified page table entry from the given entry, disabling caching for that page.
 */
FERRO_ALWAYS_INLINE uint64_t fpage_entry_disable_caching(uint64_t entry);

/**
 * Returns the address associated with the given entry.
 */
FERRO_ALWAYS_INLINE uintptr_t fpage_entry_address(uint64_t entry);

/**
 * Creates a modified entry from the given entry, marking it either as active or inactive (depending on @p active).
 */
FERRO_ALWAYS_INLINE uint64_t fpage_entry_mark_active(uint64_t entry, bool active);

/**
 * Creates a modified entry from the given entry, marking it either as privileged or unprivileged (depending on @p privileged).
 */
FERRO_ALWAYS_INLINE uint64_t fpage_entry_mark_privileged(uint64_t entry, bool privileged);

/**
 * Returns the address of the most recent page fault.
 */
FERRO_ALWAYS_INLINE uintptr_t fpage_fault_address(void);

/**
 * @}
 */

// now include the arch-dependent header
#if FERRO_ARCH == FERRO_ARCH_x86_64
	#include <ferro/core/x86_64/paging.private.h>
#elif FERRO_ARCH == FERRO_ARCH_aarch64
	#include <ferro/core/aarch64/paging.private.h>
#else
	#error Unrecognized/unsupported CPU architecture! (see <ferro/core/paging.private.h>)
#endif

FERRO_DECLARATIONS_END;

#endif // _FERRO_CORE_PAGING_PRIVATE_H_
