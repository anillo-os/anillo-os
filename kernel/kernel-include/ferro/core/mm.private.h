/*
 * This file is part of Anillo OS
 * Copyright (C) 2023 Anillo OS Developers
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
 * Memory management; private common components.
 */

#ifndef _FERRO_CORE_MM_PRIVATE_H_
#define _FERRO_CORE_MM_PRIVATE_H_

#include <stdatomic.h>

#include <ferro/core/paging.private.h>
#include <ferro/kasan.h>

FERRO_DECLARATIONS_BEGIN;

FERRO_STRUCT_FWD(fslab);

extern uint16_t fpage_root_offset_index;
extern atomic_size_t fpage_pmm_frames_in_use;
extern uint64_t fpage_pmm_total_page_count;
extern fpage_space_t fpage_vmm_kernel_address_space;
extern fpage_table_t* fpage_vmm_root_table;
extern fslab_t fpage_space_mapping_slab;

#define TABLE_ENTRY_COUNT (sizeof(((fpage_table_t*)NULL)->entries) / sizeof(*((fpage_table_t*)NULL)->entries))

// magic value used to identify pages that need to mapped on-demand
#define ON_DEMAND_MAGIC (0xdeadfeeedULL << FPAGE_VIRT_L1_SHIFT)

// coefficient that is multiplied by the amount of physical memory available to determine the maximum amount of virtual memory
// the VMM allocator can use. more virtual memory than this can be used, it's just that it'll use a less efficient method of allocation.
#define MAX_VMM_ALLOCATOR_PAGE_COUNT_COEFFICIENT 16

FERRO_OPTIONS(fpage_flags_t, fpage_private_flags) {
	fpage_private_flag_inactive = 1ull << 63,
	fpage_private_flag_repeat   = 1ull << 62,
	fpage_private_flag_kasan    = 1ull << 61,
};

#ifdef FERRO_HOST_TESTING
FERRO_ALWAYS_INLINE void* map_phys_fixed_offset(void* physical_address) {
	return physical_address;
};

FERRO_ALWAYS_INLINE void* unmap_phys_fixed_offset(void* mapped_address) {
	return mapped_address;
};
#else
FERRO_ALWAYS_INLINE void* map_phys_fixed_offset(void* physical_address) {
	return (void*)fpage_make_virtual_address(fpage_root_offset_index, FPAGE_VIRT_L3(physical_address), FPAGE_VIRT_L2(physical_address), FPAGE_VIRT_L1(physical_address), FPAGE_VIRT_OFFSET(physical_address));
};

FERRO_ALWAYS_INLINE void* unmap_phys_fixed_offset(void* mapped_address) {
	return (void*)fpage_make_virtual_address(0, FPAGE_VIRT_L3(mapped_address), FPAGE_VIRT_L2(mapped_address), FPAGE_VIRT_L1(mapped_address), FPAGE_VIRT_OFFSET(mapped_address));
};
#endif

#define map_phys_fixed_offset_type(virt) ((__typeof__((virt)))map_phys_fixed_offset((virt)))

FERRO_ALWAYS_INLINE bool fpage_space_active(fpage_space_t* space) {
	return (space == &fpage_vmm_kernel_address_space) || (space == *fpage_space_current_pointer());
};

void fpage_pmm_init(ferro_memory_region_t* memory_regions, size_t memory_region_count);
void fpage_vmm_init(void);

void* fpage_pmm_allocate_frame(size_t page_count, uint8_t alignment_power, size_t* out_allocated_page_count);
void fpage_pmm_free_frame(void* frame, size_t page_count);

#if FERRO_KASAN
bool fpage_map_kasan_shadow(void* context, uintptr_t virtual_address, uintptr_t physical_address, uint64_t page_count);

extern size_t fpage_map_kasan_pmm_allocate_marker;
#endif

void* fpage_space_allocate_virtual(fpage_space_t* space, size_t page_count, uint8_t alignment_power, size_t* out_allocated_page_count, bool user);
bool fpage_space_free_virtual(fpage_space_t* space, void* virtual, size_t page_count, bool user);
void fpage_space_flush_mapping_internal(fpage_space_t* space, void* address, size_t page_count, bool needs_flush, bool also_break, bool also_free);
void fpage_space_map_frame_fixed(fpage_space_t* space, void* phys_frame, void* virt_frame, size_t page_count, fpage_private_flags_t flags);

FERRO_DECLARATIONS_END;

#endif // _FERRO_CORE_MM_PRIVATE_H_
