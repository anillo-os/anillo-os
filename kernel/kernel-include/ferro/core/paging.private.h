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
#include <ferro/core/refcount.h>

FERRO_DECLARATIONS_BEGIN;

/**
 * @addtogroup Paging
 *
 * The paging subsystem.
 *
 * @{
 */

// free blocks are doubly-linked list nodes
//
// TODO: we always keep block lists ordered in ascending address order.
//       for more efficiency, we can turn this into an RB or AVL tree instead.
FERRO_STRUCT(fpage_free_block) {
	fpage_free_block_t** prev;
	fpage_free_block_t* next;
	size_t page_count;
};

#define FPAGE_MAX_ORDER 32
#define FPAGE_MIN_ALIGNMENT 12

FERRO_OPTIONS(uint32_t, fpage_mapping_portion_flags) {
	// The physical memory backing this portion has been allocated and
	// should be freed when the mapping is destroyed.
	// This will usually be set, but it may be absent in cases where
	// e.g. device memory has been mapped into a mapping.
	fpage_mapping_portion_flag_allocated = 1 << 0,

	// The physical memory for this mapping portion is actually contained
	// within another mapping.
	fpage_mapping_portion_flag_backing_mapping = 1 << 1,
};

// this structure should remain as small as possible.
// it is currently 48 bytes, which results in a maximum overhead of
// 1.17% of the memory allocated for a portion (if the portion contains
// a single frame, 4KiB).
FERRO_STRUCT(fpage_mapping_portion) {
	fpage_mapping_portion_t** prev;
	fpage_mapping_portion_t* next;
	union {
		uintptr_t physical_address;
		fpage_mapping_t* backing_mapping;
	};

	// making this 32 bits wide (rather than the usual 64 bits) imposes a limit of
	// `16TiB - 4KiB` on a single mapping portion. i'd say that's definitely sufficient
	// for our needs, especially since it allows us to use another 32 bits for flags and
	// avoid additional overhead per portion.
	uint32_t page_count;

	fpage_mapping_portion_flags_t flags;

	// like `page_count`, making this 32 bits limits us to a maximum offset of `16TiB - 4KiB`.
	// however, this also has the effect of limiting mappings to a maximum of 16TiB total.
	// again, this is plenty for our needs right now and this allows us to use the other 32 bits
	// for a 32-bit reference count (which allows us to free individual portions of a mapping
	// once no one needs them anymore).
	uint32_t virtual_page_offset;
	frefcount32_t refcount;

	uint32_t backing_mapping_page_offset;
	uint32_t reserved;
};

// try to keep this structure small if possible, but this one is not as critical as fpage_mapping_portion_t.
// it is currently 32 bytes.
FERRO_STRUCT(fpage_mapping) {
	frefcount32_t refcount;
	uint32_t page_count;
	fpage_mapping_flags_t flags;
	flock_spin_intsafe_t lock;
	fpage_mapping_portion_t* portions;
};

// like fpage_mapping, try to keep this structure small if possible, but this one is not as critical as fpage_mapping_portion_t.
// it is currently 56 bytes.
FERRO_STRUCT(fpage_space_mapping) {
	fpage_space_mapping_t** prev;
	fpage_space_mapping_t* next;
	fpage_mapping_t* mapping;
	uintptr_t virtual_address;
	uint32_t page_count;
	uint32_t page_offset;
	fpage_flags_t flags;
	fpage_permissions_t permissions;
};

FERRO_STRUCT(fpage_space) {
	/**
	 * The physical address of the L4 table containing all the mappings for this portion of the address space.
	 *
	 * The present entries in this table are loaded when the address space is activated and unloaded when the address space is deactivated.
	 */
	fpage_table_t* l4_table;

	flock_spin_intsafe_t lock;

	/**
	 * The head of the list of VMM blocks for this address space.
	 */
	fpage_free_block_t* blocks;
	uintptr_t vmm_allocator_start;
	uint64_t vmm_allocator_page_count;

	/**
	 * A waitq to wait for the address space to be destroyed.
	 *
	 * These waiters are notified right before any destruction is actually performed.
	 */
	fwaitq_t space_destruction_waiters;

	fpage_space_mapping_t* mappings;
};

typedef bool (*fpage_root_table_iterator)(void* context, uintptr_t virtual_address, uintptr_t physical_address, uint64_t page_count);

extern uint16_t fpage_root_recursive_index;

/**
 * Loads a page table entry.
 *
 * Examples:
 *   * `levels = 1, l4_index = 5` returns the entry of the level 4 page table (a.k.a. `l4[5]`).
 *   * `levels = 2, l4_index = 5, l3_index = 2` returns the entry of the level 3 page table at index 5 in the level 4 page table (a.k.a. `l4[5] -> l3[2]`).
 *   * `levels = 3, l4_index = 5, l3_index = 2, l2_index = 18` returns the entry of the level 2 page table at `l4[5] -> l3, l3[2] -> l2[18]`.
 *   * `levels = 4, l4_index = 5, l3_index = 2, l2_index = 18, l1_index = 3` returns the entry of the level 1 page table at `l4[5] -> l3, l3[2] -> l2, l2[18] -> l1[3]`.
 *
 * @param levels   The number of indicies to use. Must be between 1-4 (inclusive).
 * @param l4_index The L4 index to use when `levels >= 1`.
 * @param l3_index The L3 index to use when `levels >= 2`.
 * @param l2_index The L2 index to use when `levels >= 3`.
 * @param l1_index The L1 index to use when `levels >= 4`.
 */
uint64_t fpage_table_load(size_t levels, uint16_t l4_index, uint16_t l3_index, uint16_t l2_index, uint16_t l1_index);
void fpage_table_store(size_t levels, uint16_t l4_index, uint16_t l3_index, uint16_t l2_index, uint16_t l1_index, uint64_t entry);
uintptr_t fpage_table_recursive_address(size_t levels, uint16_t l4_index, uint16_t l3_index, uint16_t l2_index);

ferr_t fpage_root_table_iterate(uintptr_t start_address, uint64_t page_count, void* context, fpage_root_table_iterator iterator);

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
 * Invalidates the TLB entry/entries for the given virtual address from all online CPUs.
 */
FERRO_ALWAYS_INLINE void fpage_invalidate_tlb_for_address_all_cpus(void* address);

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
 * Invalidates the TLB entry/entries for the given range of virtual addresses (non-inclusive) from all online CPUs.
 */
FERRO_ALWAYS_INLINE void fpage_invalidate_tlb_for_range_all_cpus(void* start, void* end);

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

FERRO_ALWAYS_INLINE uint64_t fpage_entry_mark_global(uint64_t entry, bool global);

/**
 * Returns the address of the most recent page fault.
 */
FERRO_ALWAYS_INLINE uintptr_t fpage_fault_address(void);

/**
 * Invalidates all TLB entries for the current address space.
 */
FERRO_ALWAYS_INLINE void fpage_invalidate_tlb_for_active_space(void);

/**
 * Invalidates all TLB entries for the current address space on all CPUs.
 */
FERRO_ALWAYS_INLINE void fpage_invalidate_tlb_for_active_space_all_cpus(void);

/**
 * Prefault the given number of stack pages (starting from the current stack page).
 *
 * This is used to avoid page faulting due to a stack access while holding an important paging lock.
 * Faulting while holding said lock would result in a deadlock.
 *
 * @todo It might just be easier to have a separate, prebound, per-CPU stack that we use for all
 *       paging operations.
 */
FERRO_ALWAYS_INLINE void fpage_prefault_stack(size_t page_count);

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
