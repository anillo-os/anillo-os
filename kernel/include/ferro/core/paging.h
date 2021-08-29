/*
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
#include <stddef.h>

#include <ferro/base.h>
#include <ferro/platform.h>
#include <ferro/error.h>
#include <ferro/core/memory-regions.h>

FERRO_DECLARATIONS_BEGIN;

#define FERRO_KERNEL_VIRTUAL_START  ((uintptr_t)&kernel_base_virtual)
#define FERRO_KERNEL_PHYSICAL_START ((uintptr_t)&kernel_base_physical)

#define FERRO_KERNEL_VIRT_TO_PHYS(x) (((uintptr_t)x - FERRO_KERNEL_VIRTUAL_START))

#define FERRO_PAGE_ALIGNED __attribute__((aligned(4096)))

#define FPAGE_PAGE_SIZE            0x00001000ULL
#define FPAGE_LARGE_PAGE_SIZE      0x00200000ULL
#define FPAGE_VERY_LARGE_PAGE_SIZE 0x40000000ULL

#define FPAGE_LARGE_PAGE_COUNT      (FPAGE_LARGE_PAGE_SIZE / FPAGE_PAGE_SIZE)
#define FPAGE_VERY_LARGE_PAGE_COUNT (FPAGE_VERY_LARGE_PAGE_SIZE / FPAGE_PAGE_SIZE)

#define FPAGE_VIRT_L1_SHIFT 12
#define FPAGE_VIRT_L2_SHIFT 21
#define FPAGE_VIRT_L3_SHIFT 30
#define FPAGE_VIRT_L4_SHIFT 39

#define FPAGE_VIRT_OFFSET(x) ((uintptr_t)(x) & 0xfffULL)
#define FPAGE_VIRT_L1(x)     (((uintptr_t)(x) & (0x1ffULL << FPAGE_VIRT_L1_SHIFT)) >> FPAGE_VIRT_L1_SHIFT)
#define FPAGE_VIRT_L2(x)     (((uintptr_t)(x) & (0x1ffULL << FPAGE_VIRT_L2_SHIFT)) >> FPAGE_VIRT_L2_SHIFT)
#define FPAGE_VIRT_L3(x)     (((uintptr_t)(x) & (0x1ffULL << FPAGE_VIRT_L3_SHIFT)) >> FPAGE_VIRT_L3_SHIFT)
#define FPAGE_VIRT_L4(x)     (((uintptr_t)(x) & (0x1ffULL << FPAGE_VIRT_L4_SHIFT)) >> FPAGE_VIRT_L4_SHIFT)

typedef struct fpage_table fpage_table_t;
struct fpage_table {
	uint64_t entries[512];
};

extern char kernel_base_virtual;
extern char kernel_base_physical;
extern char kernel_bss_start;
extern char kernel_bss_end;

FERRO_OPTIONS(uint64_t, fpage_page_flags) {
	// Disables caching for the page(s).
	fpage_page_flag_no_cache = 1 << 0,
};

/**
 * Initializes the paging subsystem. Called on kernel startup.
 *
 * @param next_l2             Index of next L2 slot in the kernel's initial 1GiB address space.
 * @param root_table          Pointer to the root (i.e. topmost) table.
 * @param memory_regions      Array of memory region descriptors.
 * @param memory_region_count Number of descriptors in the `memory_regions` array.
 * @param image_base          Physical start address of the kernel image.
 */
void fpage_init(size_t next_l2, fpage_table_t* root_table, ferro_memory_region_t* memory_regions, size_t memory_region_count, void* image_base);

/**
 * Maps the given contiguous physical region of the given size to the next available contiguous virtual region in the kernel's address space.
 *
 * @param physical_address    Starting address of the physical region to map. If this is not page-aligned, it will be rounded down to the nearest page-aligned address.
 * @param page_count          Number of pages to map for the region.
 * @param out_virtual_address Out-pointer to the resulting mapped virtual address.
 * @param flags               Optional set of flags to modify how the page(s) is/are mapped.
 *
 * @note If `physical_address` is not page-aligned, it will automatically be rounded down to the nearest page-aligned address.
 *       In this case, the pointer written to `out_virtual_address` will ALSO be page-aligned. Therefore, you must ensure that you add any necessary offset to the result yourself.
 *
 * Return values:
 * @retval ferr_ok                The address was successfully mapped. The resulting virtual address is written to `out_virtual_address`.
 * @retval ferr_invalid_argument  One or more of the following: 1) `physical_address` was an invalid address (e.g. NULL or unsupported on the current machine), 2) `page_count` was an invalid size (e.g. 0 or too large), 3) `out_virtual_address` was NULL.
 * @retval ferr_temporary_outage  The system did not have enough memory resources to map the given address.
 */
FERRO_WUR ferr_t fpage_map_kernel_any(void* physical_address, size_t page_count, void** out_virtual_address, fpage_page_flags_t flags);

/**
 * Unmaps the virtual region of the given size identified by the given address.
 *
 * @param virtual_address Starting address of the virtual region to unmap. If this is not page-aligned, it will be rounded down to the nearest page-aligned address.
 * @param page_count      Number of pages to unmap from the region.
 *
 * Return values:
 * @retval ferr_ok                The address was successfully unmapped.
 * @retval ferr_invalid_argument  One or more of the following: 1) `virtual_address` was an invalid address (e.g. NULL or unsupported on the current machine), 2) `page_count` was an invalid size (e.g. 0 or too large).
 */
FERRO_WUR ferr_t fpage_unmap_kernel(void* virtual_address, size_t page_count);

/**
 * Maps the next available physical region(s) of the given size to the next available contiguous virtual region in the kernel's address space.
 *
 * @param page_count          Number of pages to allocate for the region.
 * @param out_virtual_address Out-pointer to the resulting mapped virtual address.
 *
 * Return values:
 * @retval ferr_ok                The address was successfully mapped. The resulting virtual address is written to `out_virtual_address`.
 * @retval ferr_invalid_argument  One or more of the following: 1) `page_count` was an invalid size (e.g. 0 or too large), 2) `out_virtual_address` was NULL.
 * @retval ferr_temporary_outage  The system did not have enough memory resources to map the given address.
 *
 * @note The resulting region *cannot* be freed using @link{fpage_unmap_kernel}. It *must* be freed using @link{fpage_free_kernel}.
 *       This is because @link{fpage_unmap_kernel} only unmaps the virtual memory, whereas @link{fpage_free_kernel} both unmaps the virtual memory and frees the physical memory.
 */
FERRO_WUR ferr_t fpage_allocate_kernel(size_t page_count, void** out_virtual_address);

/**
 * Frees the region of the given size identified by the given address previously allocated with `fpage_allocate_kernel`.
 *
 * @param virtual_address Starting address of the virtual region to free. If this is not page-aligned, it will be rounded down to the nearest page-aligned address.
 * @param page_count      Number of pages to free from the region.
 *
 * Return values:
 * @retval ferr_ok                The address was successfully unmapped.
 * @retval ferr_invalid_argument  One or more of the following: 1) `virtual_address` was an invalid address (e.g. NULL or unsupported on the current machine), 2) `page_count` was an invalid size (e.g. 0 or too large).
 */
FERRO_WUR ferr_t fpage_free_kernel(void* virtual_address, size_t page_count);

FERRO_ALWAYS_INLINE bool fpage_is_page_aligned(uintptr_t address) {
	return (address & (FPAGE_PAGE_SIZE - 1)) == 0;
};

FERRO_ALWAYS_INLINE bool fpage_is_large_page_aligned(uintptr_t address) {
	return (address & (FPAGE_LARGE_PAGE_SIZE - 1)) == 0;
};

FERRO_ALWAYS_INLINE bool fpage_is_very_large_page_aligned(uintptr_t address) {
	return (address & (FPAGE_VERY_LARGE_PAGE_SIZE - 1)) == 0;
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
 * Translates the given virtual address into a physical address. Always valid.
 */
uintptr_t fpage_virtual_to_physical(uintptr_t virtual_address);

/**
 * Determines whether an entry with the given value is active or not.
 */
FERRO_ALWAYS_INLINE bool fpage_entry_is_active(uint64_t entry_value);

/**
 * Returns an entry value that is exactly the same in almost all respects, except that it will be marked as uncacheable.
 */
FERRO_ALWAYS_INLINE uint64_t fpage_entry_disable_caching(uint64_t entry_value);

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
