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
#include <ferro/bits.h>

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

#define FPAGE_PAGE_ALIGNMENT 12

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

FERRO_STRUCT_FWD(fpage_mapping);

/**
 * A structure representing a virtual address space.
 */
FERRO_STRUCT_FWD(fpage_space);

extern char kernel_base_virtual;
extern char kernel_base_physical;
extern char kernel_bss_start;
extern char kernel_bss_end;

FERRO_OPTIONS(uint64_t, fpage_flags) {
	/**
	 * Disable caching for the pages in the region.
	 */
	fpage_flag_no_cache       = 1 << 0,

	/**
	 * Allow unprivileged (i.e. userspace) access to the region.
	 */
	fpage_flag_unprivileged = 1 << 1,

	/**
	 * Pre-bind every page in the region to a physical frame at allocation-time.
	 */
	fpage_flag_prebound = 1 << 2,

	/**
	 * Allocate pages that are zeroed out.
	 */
	fpage_flag_zero = 1 << 3,
};

FERRO_OPTIONS(uint64_t, fpage_physical_flags) {
	fpage_physical_flag_dma = 1 << 0,
};

FERRO_OPTIONS(uint8_t, fpage_permissions) {
	fpage_permission_read = 1 << 0,
	fpage_permission_write = 1 << 1,
	fpage_permission_execute = 1 << 2,
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

void fpage_log_early(void);

/**
 * Allocates the next available contiguous physical region of the given size.
 *
 * @param                    page_count Number of pages to allocate for the region.
 * @param[out] out_allocated_page_count Optional out-pointer in which to the number of pages that were actually allocated.
 * @param[out]     out_physical_address Out-pointer in which to write the start of the allocated physical region.
 * @param                         flags Optional set of flags to modify how to select regions.
 *
 * @retval ferr_ok               The region was successfully allocated. The resulting physical address has been written to @p out_physical_address.
 * @retval ferr_invalid_argument One or more of the following: 1) @p page_count was an invalid size (e.g. `0` or too large), 2) @p out_physical_address was `NULL`.
 * @retval ferr_temporary_outage The system did not have enough memory resources to allocate a physical region of the given size.
 */
FERRO_WUR ferr_t fpage_allocate_physical(size_t page_count, size_t* out_allocated_page_count, void** out_physical_address, fpage_physical_flags_t flags);

/**
 * Like fpage_allocate_physical(), but allocates memory with the specified alignment.
 *
 * @see fpage_allocate_physical
 *
 * In addition to the parameters and return values of fpage_allocate_physical():
 *
 * @param alignment_power A power of two for the alignment that the allocated region should have.
 *                        For example, for 8-byte alignment, this should be 3 because 2^3 = 8.
 *                        A value of 0 is 2^0 = 1, which is normal, unaligned memory.
 *
 * Note that, because memory is allocated in pages, the minimum alignment is always the size of the system page,
 * which 4096 bytes on most systems. Therefore, if an alignment smaller than this value is requested, it will always
 * be increased to match this value.
 *
 * @retval ferr_invalid_argument In addition to possible reasons for ferr_invalid_argument from fpage_allocate_physical(), one or more of the following: 1) @p alignment_power was an invalid power (e.g. larger than `63`)
 */
FERRO_WUR ferr_t fpage_allocate_physical_aligned(size_t page_count, uint8_t alignment_power, size_t* out_allocated_page_count, void** out_physical_address, fpage_physical_flags_t flags);

/**
 * Frees the physical region of the given size identified by the given physical address previously allocated with fpage_allocate_physical().
 *
 * @param physical_address Starting address of the physical region to free. If this is not page-aligned, it will be rounded down to the nearest page-aligned address.
 * @param page_count       Number of pages to free from the region.
 *
 * Return values:
 * @retval ferr_ok                The requested number of pages in the given physical region were successfully freed.
 * @retval ferr_invalid_argument  One or more of the following: 1) @p physical_address was an invalid address (e.g. `NULL` or unsupported on the current machine), 2) @p page_count was an invalid size (e.g. `0` or too large).
 */
FERRO_WUR ferr_t fpage_free_physical(void* physical_address, size_t page_count);

/**
 * Maps the given contiguous physical region of the given size to the next available contiguous virtual region in the kernel's address space.
 *
 * @param         physical_address Starting address of the physical region to map. If this is not page-aligned, it will be rounded down to the nearest page-aligned address.
 * @param               page_count Number of pages to map for the region.
 * @param[out] out_virtual_address Out-pointer to the resulting mapped virtual address.
 * @param                    flags Optional set of flags to modify how the page(s) is/are mapped.
 *                                 Note that the following flags are ignored:
 *                                   * fpage_flag_prebound
 *                                   * fpage_flag_zero
 *
 * @note If @p physical_address is not page-aligned, it will automatically be rounded down to the nearest page-aligned address.
 *       In this case, the pointer written to @p out_virtual_address will ALSO be page-aligned. Therefore, you must ensure that you add any necessary offset to the result yourself.
 *
 * Return values:
 * @retval ferr_ok                The address was successfully mapped. The resulting virtual address is written to @p out_virtual_address.
 * @retval ferr_invalid_argument  One or more of the following: 1) @p physical_address was an invalid address (e.g. NULL or unsupported on the current machine), 2) @p page_count was an invalid size (e.g. `0` or too large), 3) @p out_virtual_address was `NULL`.
 * @retval ferr_temporary_outage  The system did not have enough memory resources to map the given address.
 *
 * @deprecated Use fpage_space_map_any() with the fpage_space_kernel() space instead.
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
 *
 * @deprecated Use fpage_space_unmap() with the fpage_space_kernel() space instead.
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
 *
 * @deprecated Use fpage_space_allocate() with the fpage_space_kernel() space instead.
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
 *
 * @deprecated Use fpage_space_free() with the fpage_space_kernel() space instead.
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

FERRO_WUR ferr_t fpage_space_map_aligned(fpage_space_t* space, void* physical_address, size_t page_count, uint8_t alignment_power, void** out_virtual_address, fpage_flags_t flags);

/**
 * Maps the given contiguous physical region of the given size to the given contiguous virtual region in the given address space.
 *
 * @param space            The address space to map the memory in.
 * @param physical_address Starting address of the physical region to map. If this is not page-aligned, it will be rounded down to the nearest page-aligned address.
 * @param page_count       Number of pages to map for the region.
 * @param virtual_address  Starting address of the virtual region to map. If this is not page-aligned, it will be rounded down to the nearest page-aligned address.
 * @param flags            Optional set of flags to modify how the page(s) is/are mapped.
 *
 * @note This function maps the given physical region to the given virtual region **and overwrites any existing mappings**.
 *
 * Return values:
 * @retval ferr_ok               The address was successfully mapped.
 * @retval ferr_invalid_argument One or more of the following: 1) @p physical_address was an invalid address (e.g. NULL or unsupported on the current machine), 2) @p page_count was an invalid size (e.g. `0` or too large), 3) @p virtual_address was an invalid address (e.g. NULL or unsupported on the current machine).
 * @retval ferr_temporary_outage The system did not have enough memory resources to map the given address.
 */
FERRO_WUR ferr_t fpage_space_map_fixed(fpage_space_t* space, void* physical_address, size_t page_count, void* virtual_address, fpage_flags_t flags);

/**
 * Reserves a virtual region of the given size in the given address space.
 *
 * @param                    space The address space to reserve the memory in.
 * @param               page_count Number of pages to reserve for the region.
 * @param[out] out_virtual_address Out-pointer to the resulting reserved virtual address.
 *
 * @note This function reserves the a region of virtual memory for future mappings.
 *       It does NOT allocate any physical pages nor does it map any into the region.
 *       This means that the address returned by this function is NOT valid for memory access.
 *       It must first have some physical memory mapped into it with a function like fpage_space_map_fixed().
 *
 * @note The reserved region can be released with fpage_space_unmap() (even if it hasn't actually been mapped).
 *       Additionally, unmapping the region after it has been mapped will release the reservation.
 *
 * Return values:
 * @retval ferr_ok               The region was successfully reserved. The resulting virtual address has been written to @p out_virtual_address.
 * @retval ferr_invalid_argument One or more of the following: 1) @p page_count was an invalid size (e.g. `0` or too large), 2) @p out_virtual_address was `NULL`.
 * @retval ferr_temporary_outage The system did not have enough memory resources to map the given address.
 */
FERRO_WUR ferr_t fpage_space_reserve_any(fpage_space_t* space, size_t page_count, void** out_virtual_address);

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

FERRO_WUR ferr_t fpage_space_allocate_aligned(fpage_space_t* space, size_t page_count, uint8_t alignment_power, void** out_virtual_address, fpage_flags_t flags);

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
 * @param flags An optional set of flags to modify how the mapping is inserted/used.
 *              Note that the following flags are ignored:
 *                * fpage_flag_prebound
 *                * fpage_flag_zero (whether or not zeroed pages are used is configured when the mapping is created)
 */
FERRO_WUR ferr_t fpage_space_insert_mapping(fpage_space_t* space, fpage_mapping_t* mapping, size_t page_offset, size_t page_count, uint8_t alignment_power, fpage_flags_t flags, void** out_virtual_address);
FERRO_WUR ferr_t fpage_space_lookup_mapping(fpage_space_t* space, void* address, bool retain, fpage_mapping_t** out_mapping, size_t* out_page_offset, size_t* out_page_count);
FERRO_WUR ferr_t fpage_space_remove_mapping(fpage_space_t* space, void* virtual_address);
FERRO_WUR ferr_t fpage_space_move_into_mapping(fpage_space_t* space, void* address, size_t page_count, size_t page_offset, fpage_mapping_t* mapping);
FERRO_WUR ferr_t fpage_space_change_permissions(fpage_space_t* space, void* address, size_t page_count, fpage_permissions_t permissions);

FERRO_OPTIONS(uint64_t, fpage_mapping_flags) {
	/**
	 * Allocate backing pages that are zeroed out.
	 */
	fpage_mapping_flag_zero = 1 << 0,
};

FERRO_OPTIONS(uint64_t, fpage_mapping_bind_flags) {
	fpage_mapping_bind_flag_xxx_reserved = 1 << 0,
};

FERRO_WUR ferr_t fpage_mapping_retain(fpage_mapping_t* mapping);
void fpage_mapping_release(fpage_mapping_t* mapping);

FERRO_WUR ferr_t fpage_mapping_new(size_t page_count, fpage_mapping_flags_t flags, fpage_mapping_t** out_mapping);

/**
 * Binds a portion of the given mapping to the given physical memory or some newly-allocated physical memory.
 *
 * @param mapping          The mapping to operate on.
 * @param page_offset      The offset (in pages) within the mapping to start binding.
 * @param page_count       The number of pages to bind.
 * @param physical_address An optional physical address to bind the portion to.
 *                         If `NULL`, some physical memory will be allocated for the binding.
 * @param flags            An optional set of flags to modify the binding. See ::fpage_mapping_bind_flags for more information.
 */
FERRO_WUR ferr_t fpage_mapping_bind(fpage_mapping_t* mapping, size_t page_offset, size_t page_count, void* physical_address, fpage_mapping_bind_flags_t flags);

FERRO_WUR ferr_t fpage_mapping_bind_indirect(fpage_mapping_t* mapping, size_t page_offset, size_t page_count, fpage_mapping_t* target_mapping, size_t target_mapping_page_offset, fpage_mapping_bind_flags_t flags);

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
 * Retrieves the kernel address space.
 *
 * The kernel address space is always active.
 */
fpage_space_t* fpage_space_kernel(void);

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

FERRO_ALWAYS_INLINE uint64_t fpage_round_down_to_page_count(uint64_t byte_count) {
	return byte_count / FPAGE_PAGE_SIZE;
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

/**
 * Returns the address of the first boundary with the given alignment that the given region crosses.
 * If the region does not cross any boundaries with the given alignment, returns `0`.
 *
 * @note If the region starts on a boundary with the given alignment, that does not count as crossing it.
 *       Only boundaries *within* the region count as being crossed.
 *
 * @note A boundary alignment power greater than 63 is treated as having no boundary requirement and will always return `0`.
 */
FERRO_ALWAYS_INLINE uintptr_t fpage_region_boundary(uintptr_t start, size_t length, uint8_t boundary_alignment_power) {
	if (boundary_alignment_power > 63) {
		return 0;
	}
	uintptr_t boundary_alignment_mask = (1ull << boundary_alignment_power) - 1;
	uintptr_t next_boundary = (start & ~boundary_alignment_mask) + (1ull << boundary_alignment_power);
	return (next_boundary > start && next_boundary < start + length) ? next_boundary : 0;
};

FERRO_ALWAYS_INLINE uint8_t fpage_round_down_to_alignment_power(uint64_t byte_count) {
	if (byte_count == 0) {
		return 0;
	}
	return ferro_bits_in_use_u64(byte_count) - 1;
};

FERRO_ALWAYS_INLINE uint8_t fpage_round_up_to_alignment_power(uint64_t byte_count) {
	uint8_t power = fpage_round_down_to_alignment_power(byte_count);
	return ((1ull << power) < byte_count) ? (power + 1) : power;
};

FERRO_ALWAYS_INLINE uint64_t fpage_align_up(uint64_t byte_count) {
	if (byte_count == 0) {
		return 0;
	}
	return 1 << fpage_round_up_to_alignment_power(byte_count);
};

FERRO_ALWAYS_INLINE uint64_t fpage_align_down(uint64_t byte_count) {
	if (byte_count == 0) {
		return 0;
	}
	return 1 << fpage_round_down_to_alignment_power(byte_count);
};

FERRO_ALWAYS_INLINE uintptr_t fpage_align_address_down(uintptr_t address, uint8_t alignment_power) {
	return address & ~((1ull << alignment_power) - 1);
};

FERRO_ALWAYS_INLINE uintptr_t fpage_align_address_up(uintptr_t address, uint8_t alignment_power) {
	return (address + ((1ull << alignment_power) - 1)) & ~((1ull << alignment_power) - 1);
};

// these are arch-dependent functions we expect all architectures to implement

/**
 * Translates the given virtual address into a physical address. Valid after early startup; more specifically, valid once the paging subsystem has been initialized.
 *
 * @returns The physical address for the given virtual address, or `UINTPTR_MAX` if it's not a valid virtual address.
 */
uintptr_t fpage_virtual_to_physical(uintptr_t virtual_address);

/**
 * Returns `true` if the given address is canonical (i.e. a valid format for the current platform), `false` otherwise.
 */
FERRO_ALWAYS_INLINE bool fpage_address_is_canonical(uintptr_t virtual_address);

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
