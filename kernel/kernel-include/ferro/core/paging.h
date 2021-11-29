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
 * Paging subsystem.
 */

#ifndef _FERRO_CORE_PAGING_H_
#define _FERRO_CORE_PAGING_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include <ferro/base.h>
#include <ferro/platform.h>
#include <ferro/error.h>
#include <ferro/core/memory-regions.h>
#include <ferro/core/locks.h>
#include <ferro/core/waitq.h>

FERRO_DECLARATIONS_BEGIN;

/**
 * @addtogroup Paging
 *
 * The paging subsystem.
 *
 * @{
 */

#define FERRO_KERNEL_VIRTUAL_START  ((uintptr_t)0xffff800000000000)

#define FERRO_KERNEL_IMAGE_BASE FERRO_KERNEL_VIRTUAL_START

/**
 * Used to translate addresses for static data (variables, functions, etc. compiled into the kernel image)
 * into physical address offsets relative to the kernel's base address (which can be different at every load).
 */
#define FERRO_KERNEL_STATIC_TO_OFFSET(x) (((uintptr_t)x - FERRO_KERNEL_IMAGE_BASE))

#define FERRO_PAGE_ALIGNED __attribute__((aligned(4096)))

#define FPAGE_PAGE_SIZE            0x00001000ULL
#define FPAGE_LARGE_PAGE_SIZE      0x00200000ULL
#define FPAGE_VERY_LARGE_PAGE_SIZE 0x40000000ULL
#define FPAGE_SUPER_LARGE_PAGE_SIZE 0x8000000000ULL

#define FPAGE_LARGE_PAGE_COUNT      (FPAGE_LARGE_PAGE_SIZE / FPAGE_PAGE_SIZE)
#define FPAGE_VERY_LARGE_PAGE_COUNT (FPAGE_VERY_LARGE_PAGE_SIZE / FPAGE_PAGE_SIZE)
#define FPAGE_SUPER_LARGE_PAGE_COUNT (FPAGE_SUPER_LARGE_PAGE_SIZE / FPAGE_PAGE_SIZE)

#define FPAGE_VIRT_L1_SHIFT 12
#define FPAGE_VIRT_L2_SHIFT 21
#define FPAGE_VIRT_L3_SHIFT 30
#define FPAGE_VIRT_L4_SHIFT 39

#define FPAGE_VIRT_OFFSET(x) ((uintptr_t)(x) & 0xfffULL)
#define FPAGE_VIRT_L1(x)     (((uintptr_t)(x) & (0x1ffULL << FPAGE_VIRT_L1_SHIFT)) >> FPAGE_VIRT_L1_SHIFT)
#define FPAGE_VIRT_L2(x)     (((uintptr_t)(x) & (0x1ffULL << FPAGE_VIRT_L2_SHIFT)) >> FPAGE_VIRT_L2_SHIFT)
#define FPAGE_VIRT_L3(x)     (((uintptr_t)(x) & (0x1ffULL << FPAGE_VIRT_L3_SHIFT)) >> FPAGE_VIRT_L3_SHIFT)
#define FPAGE_VIRT_L4(x)     (((uintptr_t)(x) & (0x1ffULL << FPAGE_VIRT_L4_SHIFT)) >> FPAGE_VIRT_L4_SHIFT)

#define FPAGE_VIRT_VERY_LARGE_OFFSET(x) ((uintptr_t)(x) & 0x000000003fffffffULL)
#define FPAGE_VIRT_LARGE_OFFSET(x)      ((uintptr_t)(x) & 0x00000000001fffffULL)

typedef struct fpage_table fpage_table_t;
struct fpage_table {
	uint64_t entries[512];
};

#define FPAGE_USER_MAX 0x7fffffffffff
#define FPAGE_USER_L4_MAX FPAGE_VIRT_L4(FPAGE_USER_MAX)

/**
 * A structure representing a single page mapping.
 *
 * @note This structure only holds information about the mapping's virtual memory.
 *       It says nothing about how it's physically allocated.
 *
 *       To avoid duplicating information and wasting memory, physical memory information
 *       is stored directly into a page table and each process has its own page table(s).
 *       The root page table is shared between all processes (we don't take Meltdown into consideration; at least not yet)
 *       and each process has one or more L3 tables at userspace addresses (although, for now, they can only have 1 L3 table).
 */
FERRO_STRUCT(fpage_mapping) {
	fpage_mapping_t** prev;
	fpage_mapping_t* next;

	/**
	 * The virtual address at which this mapping starts.
	 */
	void* virtual_start;

	/**
	 * How many pages are in this mapping.
	 */
	size_t page_count;
};

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

/**
 * A structure representing a virtual address space.
 */
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

extern char kernel_base_virtual;
extern char kernel_base_physical;
extern char kernel_bss_start;
extern char kernel_bss_end;

FERRO_OPTIONS(uint64_t, fpage_flags) {
	/**
	 * Disables caching for the page(s).
	 */
	fpage_flag_no_cache       = 1 << 0,

	/**
	 * Allows unprivileged (i.e. userspace) access to the page(s).
	 */
	fpage_flag_unprivileged = 1 << 1,
};

/**
 * Initializes the paging subsystem. Called on kernel startup.
 *
 * @param next_l2             Index of next L2 slot in the kernel's initial 1GiB address space.
 * @param root_table          Pointer to the root (i.e. topmost) table.
 * @param memory_regions      Array of memory region descriptors.
 * @param memory_region_count Number of descriptors in the @p memory_regions array.
 * @param image_base          Physical start address of the kernel image.
 */
void fpage_init(size_t next_l2, fpage_table_t* root_table, ferro_memory_region_t* memory_regions, size_t memory_region_count, void* image_base);

/**
 * Maps the given contiguous physical region of the given size to the next available contiguous virtual region in the kernel's address space.
 *
 * @param         physical_address Starting address of the physical region to map. If this is not page-aligned, it will be rounded down to the nearest page-aligned address.
 * @param               page_count Number of pages to map for the region.
 * @param[out] out_virtual_address Out-pointer to the resulting mapped virtual address.
 * @param                    flags Optional set of flags to modify how the page(s) is/are mapped.
 *
 * @note If @p physical_address is not page-aligned, it will automatically be rounded down to the nearest page-aligned address.
 *       In this case, the pointer written to @p out_virtual_address will ALSO be page-aligned. Therefore, you must ensure that you add any necessary offset to the result yourself.
 *
 * Return values:
 * @retval ferr_ok                The address was successfully mapped. The resulting virtual address is written to @p out_virtual_address.
 * @retval ferr_invalid_argument  One or more of the following: 1) @p physical_address was an invalid address (e.g. NULL or unsupported on the current machine), 2) @p page_count was an invalid size (e.g. `0` or too large), 3) @p out_virtual_address was `NULL`.
 * @retval ferr_temporary_outage  The system did not have enough memory resources to map the given address.
 */
FERRO_WUR ferr_t fpage_map_kernel_any(void* physical_address, size_t page_count, void** out_virtual_address, fpage_flags_t flags);

/**
 * Unmaps the virtual region of the given size identified by the given address.
 *
 * @param virtual_address Starting address of the virtual region to unmap. If this is not page-aligned, it will be rounded down to the nearest page-aligned address.
 * @param page_count      Number of pages to unmap from the region.
 *
 * Return values:
 * @retval ferr_ok                The address was successfully unmapped.
 * @retval ferr_invalid_argument  One or more of the following: 1) @p virtual_address was an invalid address (e.g. `NULL` or unsupported on the current machine), 2) @p page_count was an invalid size (e.g. `0` or too large).
 */
FERRO_WUR ferr_t fpage_unmap_kernel(void* virtual_address, size_t page_count);

/**
 * Maps the next available physical region(s) of the given size to the next available contiguous virtual region in the kernel's address space.
 *
 * @param               page_count Number of pages to allocate for the region.
 * @param[out] out_virtual_address Out-pointer to the resulting mapped virtual address.
 * @param                    flags Optional set of flags to modify how the page(s) is/are mapped.
 *
 * Return values:
 * @retval ferr_ok                The address was successfully mapped. The resulting virtual address is written to @p out_virtual_address.
 * @retval ferr_invalid_argument  One or more of the following: 1) @p page_count was an invalid size (e.g. `0` or too large), 2) @p out_virtual_address was `NULL`.
 * @retval ferr_temporary_outage  The system did not have enough memory resources to map the given address.
 *
 * @note The resulting region *cannot* be freed using fpage_unmap_kernel(). It *must* be freed using fpage_free_kernel().
 *       This is because fpage_unmap_kernel() only unmaps the virtual memory, whereas fpage_free_kernel() both unmaps the virtual memory and frees the physical memory.
 */
FERRO_WUR ferr_t fpage_allocate_kernel(size_t page_count, void** out_virtual_address, fpage_flags_t flags);

/**
 * Frees the region of the given size identified by the given address previously allocated with fpage_allocate_kernel().
 *
 * @param virtual_address Starting address of the virtual region to free. If this is not page-aligned, it will be rounded down to the nearest page-aligned address.
 * @param page_count      Number of pages to free from the region.
 *
 * Return values:
 * @retval ferr_ok                The address was successfully unmapped.
 * @retval ferr_invalid_argument  One or more of the following: 1) @p virtual_address was an invalid address (e.g. `NULL` or unsupported on the current machine), 2) @p page_count was an invalid size (e.g. `0` or too large).
 */
FERRO_WUR ferr_t fpage_free_kernel(void* virtual_address, size_t page_count);

/**
 * Maps the given contiguous physical region of the given size to the next available contiguous virtual region in the given address space.
 *
 * @param                    space The address space to map the memory in.
 * @param         physical_address Starting address of the physical region to map. If this is not page-aligned, it will be rounded down to the nearest page-aligned address.
 * @param               page_count Number of pages to map for the region.
 * @param[out] out_virtual_address Out-pointer to the resulting mapped virtual address.
 * @param                    flags Optional set of flags to modify how the page(s) is/are mapped.
 *
 * @note If @p physical_address is not page-aligned, it will automatically be rounded down to the nearest page-aligned address.
 *       In this case, the pointer written to @p out_virtual_address will ALSO be page-aligned. Therefore, you must ensure that you add any necessary offset to the result yourself.
 *
 * Return values:
 * @retval ferr_ok                The address was successfully mapped. The resulting virtual address is written to @p out_virtual_address.
 * @retval ferr_invalid_argument  One or more of the following: 1) @p physical_address was an invalid address (e.g. NULL or unsupported on the current machine), 2) @p page_count was an invalid size (e.g. `0` or too large), 3) @p out_virtual_address was `NULL`.
 * @retval ferr_temporary_outage  The system did not have enough memory resources to map the given address.
 */
FERRO_WUR ferr_t fpage_space_map_any(fpage_space_t* space, void* physical_address, size_t page_count, void** out_virtual_address, fpage_flags_t flags);

/**
 * Unmaps the virtual region of the given size identified by the given address.
 *
 * @param space           The address space to unmap the memory from.
 * @param virtual_address Starting address of the virtual region to unmap. If this is not page-aligned, it will be rounded down to the nearest page-aligned address.
 * @param page_count      Number of pages to unmap from the region.
 *
 * Return values:
 * @retval ferr_ok                The address was successfully unmapped.
 * @retval ferr_invalid_argument  One or more of the following: 1) @p virtual_address was an invalid address (e.g. `NULL` or unsupported on the current machine), 2) @p page_count was an invalid size (e.g. `0` or too large).
 */
FERRO_WUR ferr_t fpage_space_unmap(fpage_space_t* space, void* virtual_address, size_t page_count);

/**
 * Maps the next available physical region(s) of the given size to the next available contiguous virtual region in the given address space.
 *
 * @param                    space The address space to allocate the memory in.
 * @param               page_count Number of pages to allocate for the region.
 * @param[out] out_virtual_address Out-pointer to the resulting mapped virtual address.
 * @param                    flags Optional set of flags to modify how the page(s) is/are mapped.
 *
 * Return values:
 * @retval ferr_ok                The address was successfully mapped. The resulting virtual address is written to @p out_virtual_address.
 * @retval ferr_invalid_argument  One or more of the following: 1) @p page_count was an invalid size (e.g. `0` or too large), 2) @p out_virtual_address was `NULL`.
 * @retval ferr_temporary_outage  The system did not have enough memory resources to map the given address.
 *
 * @note The resulting region *cannot* be freed using fpage_space_unmap(). It *must* be freed using fpage_space_free().
 *       This is because fpage_space_unmap() only unmaps the virtual memory, whereas fpage_space_free() both unmaps the virtual memory and frees the physical memory.
 */
FERRO_WUR ferr_t fpage_space_allocate(fpage_space_t* space, size_t page_count, void** out_virtual_address, fpage_flags_t flags);

/**
 * Maps the next available physical region(s) of the given size to the given contiguous virtual region in the given address space.
 *
 * @param space           The address space to allocate the memory in.
 * @param page_count      Number of pages to allocate for the region.
 * @param virtual_address The starting address of the virtual region to map.
 *                        If this is not page-aligned, it will automatically be rounded down to the nearest page-aligned address.
 * @param flags           Optional set of flags to modify how the page(s) is/are mapped.
 *
 * @retval ferr_ok The region was successfully mapped.
 * @retval ferr_invalid_argument @p page_count was an invalid size (e.g. `0` or too large).
 * @retval ferr_temporary_outage One or more of: 1) the given virtual region was already partially or fully mapped 2) the system did not have enough memory resources to map the given address.
 *
 * @note The resulting region *cannot* be freed using fpage_space_unmap(). It *must* be freed using fpage_space_free().
 *       This is because fpage_space_unmap() only unmaps the virtual memory, whereas fpage_space_free() both unmaps the virtual memory and frees the physical memory.
 */
FERRO_WUR ferr_t fpage_space_allocate_fixed(fpage_space_t* space, size_t page_count, void* virtual_address, fpage_flags_t flags);

/**
 * Frees the region of the given size identified by the given address previously allocated with fpage_space_allocate() or fpage_space_allocate_fixed().
 *
 * @param space           The address space to free the memory from.
 * @param virtual_address Starting address of the virtual region to free. If this is not page-aligned, it will be rounded down to the nearest page-aligned address.
 * @param page_count      Number of pages to free from the region.
 *
 * Return values:
 * @retval ferr_ok                The address was successfully unmapped.
 * @retval ferr_invalid_argument  One or more of the following: 1) @p virtual_address was an invalid address (e.g. `NULL` or unsupported on the current machine), 2) @p page_count was an invalid size (e.g. `0` or too large).
 */
FERRO_WUR ferr_t fpage_space_free(fpage_space_t* space, void* virtual_address, size_t page_count);

/**
 * Initializes an address space so it can be used.
 */
FERRO_WUR ferr_t fpage_space_init(fpage_space_t* space);

/**
 * Destroys an address space and frees all resources held by it.
 */
void fpage_space_destroy(fpage_space_t* space);

/**
 * Deactivates the current address space (if any) and activates the given address space.
 */
FERRO_WUR ferr_t fpage_space_swap(fpage_space_t* space);

/**
 * Retrieves the current address space.
 */
fpage_space_t* fpage_space_current(void);

/**
 * Translates the given virtual address within the given address space into a physical address.
 *
 * @returns The physical address for the given virtual address within the given address space, or `UINTPTR_MAX` if it's not a valid virtual address within the given address space.
 */
uintptr_t fpage_space_virtual_to_physical(fpage_space_t* space, uintptr_t virtual_address);

FERRO_ALWAYS_INLINE bool fpage_is_page_aligned(uintptr_t address) {
	return (address & (FPAGE_PAGE_SIZE - 1)) == 0;
};

FERRO_ALWAYS_INLINE bool fpage_is_large_page_aligned(uintptr_t address) {
	return (address & (FPAGE_LARGE_PAGE_SIZE - 1)) == 0;
};

FERRO_ALWAYS_INLINE bool fpage_is_very_large_page_aligned(uintptr_t address) {
	return (address & (FPAGE_VERY_LARGE_PAGE_SIZE - 1)) == 0;
};

FERRO_ALWAYS_INLINE bool fpage_is_super_large_page_aligned(uintptr_t address) {
	return (address & (FPAGE_SUPER_LARGE_PAGE_SIZE - 1)) == 0;
};

/**
 * Calculates the recursive virtual address for accessing a page table.
 *
 * @note This function is only meant for internal use.
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

/**
 * Round a size (in bytes) up to a multiple of the current page size.
 */
FERRO_ALWAYS_INLINE uint64_t fpage_round_up_page(uint64_t number) {
	return (number + FPAGE_PAGE_SIZE - 1) & -FPAGE_PAGE_SIZE;
};

/**
 * Round a size (in bytes) down to a multiple of the current page size.
 */
FERRO_ALWAYS_INLINE uint64_t fpage_round_down_page(uint64_t number) {
	return number & -FPAGE_PAGE_SIZE;
};

/**
 * Round the given number of bytes to a multiple of the page size, then return how many pages that is.
 *
 * e.g. If the input is 19 bytes, it'll round up to 4096 bytes, and then return 1 (because 4096 bytes is 1 page).
 */
FERRO_ALWAYS_INLINE uint64_t fpage_round_up_to_page_count(uint64_t byte_count) {
	return fpage_round_up_page(byte_count) / FPAGE_PAGE_SIZE;
};

/**
 * Returns the virtual address that contains the lookup information provided.
 */
FERRO_ALWAYS_INLINE uintptr_t fpage_make_virtual_address(size_t l4_index, size_t l3_index, size_t l2_index, size_t l1_index, uintptr_t offset) {
	uintptr_t result = 0;

	result |= (l4_index & 0x1ffULL) << FPAGE_VIRT_L4_SHIFT;
	result |= (l3_index & 0x1ffULL) << FPAGE_VIRT_L3_SHIFT;
	result |= (l2_index & 0x1ffULL) << FPAGE_VIRT_L2_SHIFT;
	result |= (l1_index & 0x1ffULL) << FPAGE_VIRT_L1_SHIFT;
	result |= offset & 0xfffULL;

	if (result & (1ULL << 47)) {
		result |= 0xffffULL << 48;
	}

	return result;
};

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
 * Translates the given virtual address into a physical address. Valid after early startup; more specifically, valid once the paging subsystem has been initialized.
 *
 * @returns The physical address for the given virtual address, or `UINTPTR_MAX` if it's not a valid virtual address.
 */
uintptr_t fpage_virtual_to_physical(uintptr_t virtual_address);

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
 * @}
 */

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
