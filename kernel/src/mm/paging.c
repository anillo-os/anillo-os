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
 * Virtual memory allocation.
 */

#include <ferro/core/paging.private.h>
#include <ferro/core/locks.h>
#include <ferro/bits.h>
#include <ferro/core/panic.h>
#include <libsimple/libsimple.h>
#include <stdatomic.h>
#include <ferro/core/interrupts.h>
#include <ferro/core/console.h>
#include <ferro/core/threads.private.h>
#include <ferro/core/mm.private.h>
#include <ferro/mm/slab.h>
#include <ferro/kasan.h>

// how many pages to prefault when doing a prefault
#define PREFAULT_PAGE_COUNT 2

// altogether, we've reserved 2 L4 indicies, which means that the maximum amount of memory
// we can use is 256TiB - (2 * 512GiB) = 255TiB.
// yeah, i think we're okay for now.

fpage_table_t* fpage_vmm_root_table = NULL;
// the L4 index for the kernel's address space
static uint16_t kernel_l4_index = 0;
// the L3 index for the kernel's initial memory region
static uint16_t kernel_l3_index = 0;
uint16_t fpage_root_recursive_index = TABLE_ENTRY_COUNT - 1;

fslab_t fpage_space_mapping_slab = FSLAB_INITIALIZER_TYPE(fpage_space_mapping_t);
static fslab_t fpage_mapping_portion_slab = FSLAB_INITIALIZER_TYPE(fpage_mapping_portion_t);
static fslab_t fpage_mapping_slab = FSLAB_INITIALIZER_TYPE(fpage_mapping_t);

static fpage_table_t kernel_address_space_root_table = {0};

// we're never going to get more physical memory, so the regions head is never going to be modified; thus, we don't need a lock
//static flock_spin_intsafe_t regions_head_lock = FLOCK_SPIN_INTSAFE_INIT;

/**
 * Used to map 512GiB of memory at a fixed offset.
 */
FERRO_PAGE_ALIGNED
static fpage_table_t offset_table = {0};
uint16_t fpage_root_offset_index = FPAGE_VIRT_L4(FERRO_KERNEL_VIRTUAL_START) + 1;

#ifndef FPAGE_DEBUG_ALWAYS_PREBIND
	#define FPAGE_DEBUG_ALWAYS_PREBIND 0
#endif

#ifndef FPAGE_DEBUG_LOG_FAULTS
	#define FPAGE_DEBUG_LOG_FAULTS 0
#endif

#ifndef FPAGE_DEBUG_LOG_FRAMES
	#define FPAGE_DEBUG_LOG_FRAMES 0
#endif

#ifndef FPAGE_DEBUG_ENSURE_BLOCK_STATE
	#define FPAGE_DEBUG_ENSURE_BLOCK_STATE 0
#endif

static void page_fault_handler(void* context);

bool fpage_prefaulting_enabled = false;
bool fpage_logging_available = false;

void fpage_prefault_enable(void) {
	fpage_prefaulting_enabled = true;
};

void fpage_logging_mark_available(void) {
	fpage_logging_available = true;
};

uintptr_t fpage_table_recursive_address(size_t levels, uint16_t l4_index, uint16_t l3_index, uint16_t l2_index) {
	if (levels == 0) {
		return fpage_make_virtual_address(fpage_root_recursive_index, fpage_root_recursive_index, fpage_root_recursive_index, fpage_root_recursive_index, 0);
	} else if (levels == 1) {
		return fpage_make_virtual_address(fpage_root_recursive_index, fpage_root_recursive_index, fpage_root_recursive_index, l4_index, 0);
	} else if (levels == 2) {
		return fpage_make_virtual_address(fpage_root_recursive_index, fpage_root_recursive_index, l4_index, l3_index, 0);
	} else if (levels == 3) {
		return fpage_make_virtual_address(fpage_root_recursive_index, l4_index, l3_index, l2_index, 0);
	} else {
		return 0;
	}
};

FERRO_NO_KASAN
uint64_t fpage_table_load(size_t levels, uint16_t l4_index, uint16_t l3_index, uint16_t l2_index, uint16_t l1_index) {
	fpage_table_t* table = (void*)fpage_table_recursive_address(levels - 1, l4_index, l3_index, l2_index);
	uint16_t final_index = 0;

	switch (levels) {
		case 1: final_index = l4_index; break;
		case 2: final_index = l3_index; break;
		case 3: final_index = l2_index; break;
		case 4: final_index = l1_index; break;
	}

	return table->entries[final_index];
};

FERRO_NO_KASAN
void fpage_table_store(size_t levels, uint16_t l4_index, uint16_t l3_index, uint16_t l2_index, uint16_t l1_index, uint64_t entry) {
	fpage_table_t* table = (void*)fpage_table_recursive_address(levels - 1, l4_index, l3_index, l2_index);
	uint16_t final_index = 0;

	switch (levels) {
		case 1: final_index = l4_index; break;
		case 2: final_index = l3_index; break;
		case 3: final_index = l2_index; break;
		case 4: final_index = l1_index; break;
	}

	table->entries[final_index] = entry;
};

FERRO_NO_KASAN
ferr_t fpage_root_table_iterate(uintptr_t address, uint64_t page_count, void* context, fpage_root_table_iterator iterator) {
	address &= ~(FPAGE_PAGE_SIZE - 1);

	while (page_count > 0) {
		uint16_t l4 = FPAGE_VIRT_L4(address);
		uint16_t l3 = FPAGE_VIRT_L3(address);
		uint16_t l2 = FPAGE_VIRT_L2(address);
		uint16_t l1 = FPAGE_VIRT_L1(address);
		uint64_t entry = 0;

		entry = fpage_table_load(1, l4, 0, 0, 0);

		// check if L4 is active
		if (!fpage_entry_is_active(entry)) {
			page_count -= (page_count < FPAGE_SUPER_LARGE_PAGE_COUNT) ? page_count : FPAGE_SUPER_LARGE_PAGE_COUNT;
			address += FPAGE_SUPER_LARGE_PAGE_SIZE;
			continue;
		}

		// at L4, large pages are not allowed, so no need to check

		entry = fpage_table_load(2, l4, l3, 0, 0);

		// check if L3 is active
		if (!fpage_entry_is_active(entry)) {
			page_count -= (page_count < FPAGE_VERY_LARGE_PAGE_COUNT) ? page_count : FPAGE_VERY_LARGE_PAGE_COUNT;
			address += FPAGE_VERY_LARGE_PAGE_SIZE;
			continue;
		}

		// at L3, there might be a very large page instead of a table
		if (fpage_entry_is_large_page_entry(entry)) {
			if (!iterator(context, fpage_make_virtual_address(l4, l3, 0, 0, 0), fpage_entry_address(entry), FPAGE_VERY_LARGE_PAGE_COUNT)) {
				return ferr_cancelled;
			}

			page_count -= (page_count < FPAGE_VERY_LARGE_PAGE_COUNT) ? page_count : FPAGE_VERY_LARGE_PAGE_COUNT;
			address += FPAGE_VERY_LARGE_PAGE_SIZE;
			continue;
		}

		entry = fpage_table_load(3, l4, l3, l2, 0);

		// check if L2 is active
		if (!fpage_entry_is_active(entry)) {
			page_count -= (page_count < FPAGE_LARGE_PAGE_COUNT) ? page_count : FPAGE_LARGE_PAGE_COUNT;
			address += FPAGE_LARGE_PAGE_SIZE;
			continue;
		}

		// at L2, there might be a large page instead of a table
		if (fpage_entry_is_large_page_entry(entry)) {
			if (!iterator(context, fpage_make_virtual_address(l4, l3, l2, 0, 0), fpage_entry_address(entry), FPAGE_LARGE_PAGE_COUNT)) {
				return ferr_cancelled;
			}

			page_count -= (page_count < FPAGE_LARGE_PAGE_COUNT) ? page_count : FPAGE_LARGE_PAGE_COUNT;
			address += FPAGE_LARGE_PAGE_SIZE;
			continue;
		}

		entry = fpage_table_load(4, l4, l3, l2, l1);

		// check if L1 is active
		if (!fpage_entry_is_active(entry)) {
			--page_count;
			address += FPAGE_PAGE_SIZE;
			continue;
		}

		if (!iterator(context, fpage_make_virtual_address(l4, l3, l2, l1, 0), fpage_entry_address(entry), 1)) {
			return ferr_cancelled;
		}

		--page_count;
		address += FPAGE_PAGE_SIZE;
	}

	return ferr_ok;
};

#if FERRO_KASAN
size_t fpage_map_kasan_pmm_allocate_marker = 0;

FERRO_NO_KASAN
bool fpage_map_kasan_shadow(void* context, uintptr_t virtual_address, uintptr_t physical_address, uint64_t page_count) {
	if (
		(FPAGE_VIRT_L4(virtual_address) == fpage_root_offset_index && page_count >= FPAGE_VERY_LARGE_PAGE_COUNT) ||
		FPAGE_VIRT_L4(virtual_address) == fpage_root_recursive_index ||
		FPAGE_VIRT_L4(virtual_address) == FPAGE_VIRT_L4(FERRO_KASAN_SHADOW_BASE)
	) {
		return true;
	}

	while (page_count > 0) {
		uintptr_t shadow = ferro_kasan_shadow_for_pointer(virtual_address);
		uintptr_t shadow_page = shadow & ~(FPAGE_PAGE_SIZE - 1);

		if (fpage_virtual_to_physical(shadow_page) == UINTPTR_MAX) {
			void* frame = fpage_pmm_allocate_frame(1, 0, &fpage_map_kasan_pmm_allocate_marker);
			if (!frame) {
				fpanic("Failed to allocate KASan shadow page");
			}
			fpage_space_map_frame_fixed(&fpage_vmm_kernel_address_space, frame, (void*)shadow_page, 1, fpage_private_flag_kasan);
			ferro_kasan_fill_unchecked((void*)shadow_page, 0, FPAGE_PAGE_SIZE);
		}

		--page_count;
		virtual_address += FPAGE_PAGE_SIZE;
	}

	return true;
};
#endif

// we don't need to worry about locks in this function; interrupts are disabled and we're in a uniprocessor environment
FERRO_NO_KASAN
void fpage_init(size_t next_l2, fpage_table_t* table, ferro_memory_region_t* memory_regions, size_t memory_region_count, void* image_base) {
	fpage_table_t* l2_table = NULL;
	fpage_space_mapping_t* kasan_mapping = NULL;

	// initialize the address space pointer with the kernel address space
	*fpage_space_current_pointer() = &fpage_vmm_kernel_address_space;

	fpage_vmm_root_table = table;
	kernel_l4_index = FPAGE_VIRT_L4(FERRO_KERNEL_VIRTUAL_START);
	kernel_l3_index = FPAGE_VIRT_L3(FERRO_KERNEL_VIRTUAL_START);

	// determine the correct recursive index
	while (fpage_vmm_root_table->entries[fpage_root_recursive_index] != 0) {
		--fpage_root_recursive_index;

		if (fpage_root_recursive_index == 0) {
			// well, crap. we can't go lower than 0. just overwrite whatever's at 0.
			break;
		}
	}

	// set up the recursive mapping
	// can't use fpage_virtual_to_physical() for the physical address lookup because it depends on the recursive entry (which is what we're setting up right now).
	//
	// this should remain a privileged table, so that unprivileged code can't modify page tables willy-nilly
	fpage_vmm_root_table->entries[fpage_root_recursive_index] = fpage_entry_disable_caching(fpage_table_entry(FERRO_KERNEL_STATIC_TO_OFFSET(fpage_vmm_root_table) + (uintptr_t)image_base, true));
	fpage_synchronize_after_table_modification();

	// we can use the recursive virtual address for the table now
	fpage_vmm_root_table = (fpage_table_t*)fpage_table_recursive_address(0, 0, 0, 0);

	// map all the physical memory at a fixed offset.
	// we assume it's 512GiB or less; no consumer device supports more than 128GiB currently.
	// we can always add more later.

	// determine the correct offset index
	while (fpage_vmm_root_table->entries[fpage_root_offset_index] != 0) {
		--fpage_root_offset_index;

		if (fpage_root_offset_index == 0) {
			// well, crap. we can't go lower than 0. just overwrite whatever's at 0.
			break;
		}
	}

	for (size_t i = 0; i < TABLE_ENTRY_COUNT; ++i) {
		offset_table.entries[i] = fpage_entry_mark_global(fpage_very_large_page_entry(i * FPAGE_VERY_LARGE_PAGE_SIZE, true), true);
	}

	// this also remains a privileged table so that unprivileged code can't access physical memory directly
	fpage_vmm_root_table->entries[fpage_root_offset_index] = fpage_entry_disable_caching(fpage_table_entry(fpage_virtual_to_physical((uintptr_t)&offset_table), true));
	fpage_synchronize_after_table_modification();

#if FERRO_KASAN
	// our current KASan implementation expects to run on a single CPU
	fassert(fcpu_count() == 1);
#endif

	fpage_pmm_init(memory_regions, memory_region_count);

	// address spaces store *physical* addresses, not virtual ones
	fpage_vmm_kernel_address_space.l4_table = (void*)fpage_virtual_to_physical((uintptr_t)&kernel_address_space_root_table);

	// initialize the kernel address space root table with the root table
	for (size_t i = FPAGE_VIRT_L4(FERRO_KERNEL_VIRTUAL_START); i < TABLE_ENTRY_COUNT; ++i) {
		kernel_address_space_root_table.entries[i] = fpage_vmm_root_table->entries[i];
	}

	// ignore the recursive and offset table indicies
	// (so that we don't change them when swapping page spaces)
	kernel_address_space_root_table.entries[fpage_root_recursive_index] = 0;
	kernel_address_space_root_table.entries[fpage_root_offset_index] = 0;

#if FERRO_KASAN
	// map the corresponding KASan shadow regions for all currently mapped regions in the higher-half
	fpage_root_table_iterate(FERRO_KERNEL_VIRTUAL_START, ((UINT64_MAX - FERRO_KERNEL_VIRTUAL_START) + 1) / FPAGE_PAGE_SIZE, NULL, fpage_map_kasan_shadow);

	// map the KASan shadow for the kernel's L4 table
	fpage_map_kasan_shadow(NULL, (uintptr_t)map_phys_fixed_offset(fpage_vmm_kernel_address_space.l4_table), (uintptr_t)fpage_vmm_kernel_address_space.l4_table, 1);
#endif

	fpage_vmm_init();

	// register our page fault handler
	fpanic_status(fint_register_special_handler(fint_special_interrupt_page_fault, page_fault_handler, NULL));
};

ferr_t fpage_allocate_physical(size_t page_count, size_t* out_allocated_page_count, void** out_physical_address, fpage_physical_flags_t flags) {
	return fpage_allocate_physical_aligned(page_count, 0, out_allocated_page_count, out_physical_address, flags);
};

ferr_t fpage_allocate_physical_aligned(size_t page_count, uint8_t alignment_power, size_t* out_allocated_page_count, void** out_physical_address, fpage_physical_flags_t flags) {
	ferr_t status = ferr_ok;
	size_t allocated = 0;
	void* frame = NULL;

	if (!out_physical_address) {
		status = ferr_invalid_argument;
		goto out;
	}

	frame = fpage_pmm_allocate_frame(page_count, alignment_power, &allocated);
	if (!frame) {
		status = ferr_temporary_outage;
		goto out;
	}

out:
	if (status == ferr_ok) {
		*out_physical_address = frame;
		if (out_allocated_page_count) {
			*out_allocated_page_count = allocated;
		}
	}
	return status;
};

ferr_t fpage_free_physical(void* physical_address, size_t page_count) {
	ferr_t status = ferr_ok;

	if (!physical_address) {
		status = ferr_invalid_argument;
	}

	fpage_pmm_free_frame(physical_address, page_count);

out:
	return status;
};

ferr_t fpage_map_kernel_any(void* physical_address, size_t page_count, void** out_virtual_address, fpage_flags_t flags) {
	return fpage_space_map_any(&fpage_vmm_kernel_address_space, physical_address, page_count, out_virtual_address, flags);
};

ferr_t fpage_unmap_kernel(void* virtual_address, size_t page_count) {
	return fpage_space_unmap(&fpage_vmm_kernel_address_space, virtual_address, page_count);
};

ferr_t fpage_allocate_kernel(size_t page_count, void** out_virtual_address, fpage_flags_t flags) {
	return fpage_space_allocate(&fpage_vmm_kernel_address_space, page_count, out_virtual_address, flags);
};

ferr_t fpage_free_kernel(void* virtual_address, size_t page_count) {
	return fpage_space_free(&fpage_vmm_kernel_address_space, virtual_address, page_count);
};

FERRO_WUR ferr_t fpage_space_swap(fpage_space_t* space) {
	if (!space) {
		space = &fpage_vmm_kernel_address_space;
	}

	fint_disable();

	fpage_space_t** current_address_space = fpage_space_current_pointer();

	if (*current_address_space == space) {
		goto out_locked;
	}

	// we never unload the kernel address space
	if (*current_address_space && *current_address_space != fpage_space_kernel()) {
		fpage_table_t* temp_table = map_phys_fixed_offset((*current_address_space)->l4_table);

		fpage_prefault_stack(PREFAULT_PAGE_COUNT);
		flock_spin_intsafe_lock(&(*current_address_space)->lock);

		for (size_t i = 0; i < TABLE_ENTRY_COUNT; ++i) {
			uint64_t entry = temp_table->entries[i];

			if (!fpage_entry_is_active(entry)) {
				continue;
			}

			fpage_table_store(1, i, 0, 0, 0, 0);
		}

		flock_spin_intsafe_unlock(&(*current_address_space)->lock);

		// FIXME: the precise table flush (fpage_flush_table()) isn't working, so we're doing a full table flush as a workaround for now.
		//        on x86_64, we could mitigate the performance impact by making kernel addresses "global" entries in the page tables.
		fpage_invalidate_tlb_for_active_space();
	}

	*current_address_space = space;

	if ((*current_address_space)) {
		fpage_table_t* temp_table = map_phys_fixed_offset((*current_address_space)->l4_table);

		fpage_prefault_stack(PREFAULT_PAGE_COUNT);
		flock_spin_intsafe_lock(&(*current_address_space)->lock);

		for (size_t i = 0; i < TABLE_ENTRY_COUNT; ++i) {
			uint64_t entry = temp_table->entries[i];

			if (!fpage_entry_is_active(entry)) {
				continue;
			}

			fpage_table_store(1, i, 0, 0, 0, entry);
		}

		flock_spin_intsafe_unlock(&(*current_address_space)->lock);
	}

out_locked:
	fint_enable();

	return ferr_ok;
};

fpage_space_t* fpage_space_current(void) {
	fint_disable();
	fpage_space_t* current_address_space = *fpage_space_current_pointer();
	fint_enable();
	return current_address_space;
};

fpage_space_t* fpage_space_kernel(void) {
	return &fpage_vmm_kernel_address_space;
};

ferr_t fpage_space_map_aligned(fpage_space_t* space, void* physical_address, size_t page_count, uint8_t alignment_power, void** out_virtual_address, fpage_flags_t flags) {
	void* virt = NULL;

	if (physical_address == NULL || page_count == 0 || page_count == SIZE_MAX || out_virtual_address == NULL) {
		return ferr_invalid_argument;
	}

	fpage_prefault_stack(PREFAULT_PAGE_COUNT);
	flock_spin_intsafe_lock(&space->lock);

	virt = fpage_space_allocate_virtual(space, page_count, alignment_power, NULL, false);

	if (!virt) {
		flock_spin_intsafe_unlock(&space->lock);
		return ferr_temporary_outage;
	}

	fpage_space_map_frame_fixed(space, physical_address, virt, page_count, flags);

	flock_spin_intsafe_unlock(&space->lock);

	*out_virtual_address = virt;

	return ferr_ok;
};

ferr_t fpage_space_map_any(fpage_space_t* space, void* physical_address, size_t page_count, void** out_virtual_address, fpage_flags_t flags) {
	return fpage_space_map_aligned(space, physical_address, page_count, 0, out_virtual_address, flags);
};

ferr_t fpage_space_unmap(fpage_space_t* space, void* virtual_address, size_t page_count) {
	if (virtual_address == NULL || page_count == 0 || page_count == SIZE_MAX) {
		return ferr_invalid_argument;
	}

	fpage_prefault_stack(PREFAULT_PAGE_COUNT);
	flock_spin_intsafe_lock(&space->lock);

	fpage_space_flush_mapping_internal(space, virtual_address, page_count, fpage_space_active(space), true, false);

	fpage_space_free_virtual(space, virtual_address, page_count, false);

	flock_spin_intsafe_unlock(&space->lock);

	return ferr_ok;
};

ferr_t fpage_space_allocate_aligned(fpage_space_t* space, size_t page_count, uint8_t alignment_power, void** out_virtual_address, fpage_flags_t flags) {
	void* virt = NULL;
	ferr_t status = ferr_ok;
	fpage_space_mapping_t* space_mapping = NULL;
	bool release_lock = false;

#if FPAGE_DEBUG_ALWAYS_PREBIND
	flags |= fpage_flag_prebound;
#endif

	if (page_count == 0 || page_count == SIZE_MAX || out_virtual_address == NULL) {
		return ferr_invalid_argument;
	}

	if ((flags & fpage_flag_prebound) == 0) {
		status = fslab_allocate(&fpage_space_mapping_slab, (void*)&space_mapping);
		if (status != ferr_ok) {
			goto out;
		}
	}

	fpage_prefault_stack(PREFAULT_PAGE_COUNT);
	flock_spin_intsafe_lock(&space->lock);
	release_lock = true;

	// NOTE: allocating fixed addresses within the buddy allocator's region(s) is not allowed,
	//       so there is no need to acquire the allocation lock here.
	//       the buddy allocator already has its own locks.

	virt = fpage_space_allocate_virtual(space, page_count, alignment_power, NULL, false);

	if (!virt) {
		flock_spin_intsafe_unlock(&space->lock);
		return ferr_temporary_outage;
	}

	if (flags & fpage_flag_prebound) {
		for (size_t i = 0; i < page_count; ++i) {
			void* frame = fpage_pmm_allocate_frame(1, 0, NULL);

			if (!frame) {
				for (; i > 0; --i) {
					uintptr_t virt_frame = (uintptr_t)virt + ((i - 1) * FPAGE_PAGE_SIZE);
					fpage_pmm_free_frame((void*)fpage_space_virtual_to_physical(space, virt_frame), 1);
					fpage_space_flush_mapping_internal(space, (void*)virt_frame, 1, fpage_space_active(space), true, false);
				}
				fpage_space_free_virtual(space, virt, page_count, false);
				flock_spin_intsafe_unlock(&space->lock);
				return ferr_temporary_outage;
			}

			fpage_space_map_frame_fixed(space, frame, (void*)((uintptr_t)virt + (i * FPAGE_PAGE_SIZE)), 1, flags);
		}

		if (flags & fpage_flag_zero) {
			// zero out the memory now, since we're prebinding
			simple_memset(virt, 0, page_count * FPAGE_PAGE_SIZE);
		}
	} else {
		fpage_space_map_frame_fixed(space, (void*)ON_DEMAND_MAGIC, virt, page_count, flags | fpage_private_flag_inactive | fpage_private_flag_repeat);

		space_mapping->prev = &space->mappings;
		space_mapping->next = *space_mapping->prev;

		if (space_mapping->next) {
			space_mapping->next->prev = &space_mapping->next;
		}
		*space_mapping->prev = space_mapping;

		space_mapping->mapping = NULL;
		space_mapping->virtual_address = (uintptr_t)virt;
		space_mapping->page_count = page_count;
		space_mapping->page_offset = 0;
		space_mapping->flags = flags;
	}

	flock_spin_intsafe_unlock(&space->lock);
	release_lock = false;

out:
	if (status == ferr_ok) {
		*out_virtual_address = virt;
	} else {
		if (virt) {
			fpage_space_free_virtual(space, virt, page_count, false);
		}

		if (release_lock) {
			flock_spin_intsafe_unlock(&space->lock);
		}

		if (space_mapping) {
			FERRO_WUR_IGNORE(fslab_free(&fpage_space_mapping_slab, space_mapping));
		}
	}

	return status;
};

ferr_t fpage_space_allocate(fpage_space_t* space, size_t page_count, void** out_virtual_address, fpage_flags_t flags) {
	return fpage_space_allocate_aligned(space, page_count, 0, out_virtual_address, flags);
};

// MUST be holding the L4 table lock
static bool space_region_is_free(fpage_space_t* space, uintptr_t virtual_address, size_t page_count) {
	while (page_count > 0) {
		uint16_t l4 = FPAGE_VIRT_L4(virtual_address);
		uint16_t l3 = FPAGE_VIRT_L3(virtual_address);
		uint16_t l2 = FPAGE_VIRT_L2(virtual_address);
		uint16_t l1 = FPAGE_VIRT_L1(virtual_address);
		uint16_t offset = FPAGE_VIRT_OFFSET(virtual_address);

		fpage_table_t* table = map_phys_fixed_offset(space->l4_table);
		uint64_t entry = table->entries[l4];

		// L4 table

		if (!fpage_entry_is_active(entry)) {
			// if the free region in the table has more pages in it, we already know
			// that the entire region is free
			if (page_count < FPAGE_SUPER_LARGE_PAGE_COUNT) {
				return true;
			}

			page_count -= FPAGE_SUPER_LARGE_PAGE_COUNT;
			virtual_address += FPAGE_SUPER_LARGE_PAGE_SIZE;
			continue;
		}

		table = map_phys_fixed_offset((void*)fpage_entry_address(entry));
		entry = table->entries[l3];

		// L3 table

		if (!fpage_entry_is_active(entry) && fpage_entry_address(entry) != ON_DEMAND_MAGIC) {
			// same as the L4 case
			if (page_count < FPAGE_VERY_LARGE_PAGE_COUNT) {
				return true;
			}

			page_count -= FPAGE_VERY_LARGE_PAGE_COUNT;
			virtual_address += FPAGE_VERY_LARGE_PAGE_SIZE;
			continue;
		}

		if (fpage_entry_is_large_page_entry(entry)) {
			// if this is a large entry and it's active (or bound-on-demand), the region is partially or fully in-use.
			return false;
		}

		// on-demand binding is only valid for page table leaves (i.e. very large, large, or normal pages)
		fassert(fpage_entry_is_active(entry));

		table = map_phys_fixed_offset((void*)fpage_entry_address(entry));
		entry = table->entries[l2];

		// L2 table

		if (!fpage_entry_is_active(entry) && fpage_entry_address(entry) != ON_DEMAND_MAGIC) {
			// same as the L4 case
			if (page_count < FPAGE_LARGE_PAGE_COUNT) {
				return true;
			}

			page_count -= FPAGE_LARGE_PAGE_COUNT;
			virtual_address += FPAGE_LARGE_PAGE_COUNT;
			continue;
		}

		if (fpage_entry_is_large_page_entry(entry)) {
			// same as the L3 case
			return false;
		}

		// same as the L3 case
		fassert(fpage_entry_is_active(entry));

		table = map_phys_fixed_offset((void*)fpage_entry_address(entry));
		entry = table->entries[l1];

		// L1 table

		if (!fpage_entry_is_active(entry) && fpage_entry_address(entry) != ON_DEMAND_MAGIC) {
			// the entry is inactive, so it's free; let's keep checking
			--page_count;
			virtual_address += FPAGE_PAGE_SIZE;
			continue;
		}

		return false;
	}

	// all the entries were free, so the region is free
	return true;
};

bool space_region_belongs_to_vmm_allocator(fpage_space_t* space, void* virtual_start, size_t page_count) {
	uintptr_t start = (uintptr_t)virtual_start;
	uintptr_t end = fpage_round_down_page(start) + (page_count * FPAGE_PAGE_SIZE);
	uintptr_t vmm_start = space->vmm_allocator_start;
	uintptr_t vmm_end = vmm_start + (space->vmm_allocator_page_count * FPAGE_PAGE_SIZE);

	return (start >= vmm_start && start < vmm_end) || (end > vmm_start && end <= vmm_end);
};

ferr_t fpage_space_allocate_fixed(fpage_space_t* space, size_t page_count, void* virtual_address, fpage_flags_t flags) {
	ferr_t status = ferr_ok;
	fpage_space_mapping_t* space_mapping = NULL;

#if FPAGE_DEBUG_ALWAYS_PREBIND
	flags |= fpage_flag_prebound;
#endif

	fpage_prefault_stack(PREFAULT_PAGE_COUNT);
	flock_spin_intsafe_lock(&space->lock);

	// if it's in the buddy allocator's region(s), it's reserved for the buddy allocator and can't be mapped for anyone else
	// TODO: allow this to be mapped by allocating it with the buddy allocator
	if (space_region_belongs_to_vmm_allocator(space, virtual_address, page_count)) {
		status = ferr_temporary_outage;
		goto out;
	}

	if ((flags & fpage_flag_prebound) == 0) {
		status = fslab_allocate(&fpage_space_mapping_slab, (void*)&space_mapping);
		if (status != ferr_ok) {
			goto out;
		}
	}

	if (!space_region_is_free(space, (uintptr_t)virtual_address, page_count)) {
		status = ferr_temporary_outage;
		goto out;
	}

	if (flags & fpage_flag_prebound) {
		for (size_t i = 0; i < page_count; ++i) {
			void* frame = fpage_pmm_allocate_frame(1, 0, NULL);

			if (!frame) {
				for (; i > 0; --i) {
					uintptr_t virt_frame = (uintptr_t)virtual_address + ((i - 1) * FPAGE_PAGE_SIZE);
					fpage_pmm_free_frame((void*)fpage_space_virtual_to_physical(space, virt_frame), 1);
					fpage_space_flush_mapping_internal(space, (void*)virt_frame, 1, fpage_space_active(space), true, false);
				}
				status = ferr_temporary_outage;
				goto out;
			}

			fpage_space_map_frame_fixed(space, frame, (void*)((uintptr_t)virtual_address + (i * FPAGE_PAGE_SIZE)), 1, flags);
		}

		if (flags & fpage_flag_zero) {
			// zero out the memory now, since we're prebinding
			simple_memset(virtual_address, 0, page_count * FPAGE_PAGE_SIZE);
		}
	} else {
		fpage_space_map_frame_fixed(space, (void*)ON_DEMAND_MAGIC, virtual_address, page_count, flags | fpage_private_flag_inactive | fpage_private_flag_repeat);

		space_mapping->prev = &space->mappings;
		space_mapping->next = *space_mapping->prev;

		if (space_mapping->next) {
			space_mapping->next->prev = &space_mapping->next;
		}
		*space_mapping->prev = space_mapping;

		space_mapping->mapping = NULL;
		space_mapping->virtual_address = (uintptr_t)virtual_address;
		space_mapping->page_count = page_count;
		space_mapping->page_offset = 0;
		space_mapping->flags = flags;
	}

out:
	flock_spin_intsafe_unlock(&space->lock);
	if (status != ferr_ok) {
		if (space_mapping) {
			FERRO_WUR_IGNORE(fslab_free(&fpage_space_mapping_slab, space_mapping));
		}
	}
	return status;
};

ferr_t fpage_space_free(fpage_space_t* space, void* virtual_address, size_t page_count) {
	fpage_space_mapping_t* mapping = NULL;

	if (virtual_address == NULL || page_count == 0 || page_count == SIZE_MAX) {
		return ferr_invalid_argument;
	}

	// TODO: check whether we can safely remove the mapping without holding the L4 table lock
	//       (only locking it later on, when we flush and break the mapping)
	fpage_prefault_stack(PREFAULT_PAGE_COUNT);
	flock_spin_intsafe_lock(&space->lock);

	for (mapping = space->mappings; mapping != NULL; mapping = mapping->next) {
		if (
			mapping->virtual_address <= (uintptr_t)virtual_address &&
			mapping->virtual_address + (mapping->page_count * FPAGE_PAGE_SIZE) >= (uintptr_t)virtual_address + (page_count * FPAGE_PAGE_SIZE)
		) {
			// this is the mapping that contains the target address

			// TODO: maybe add support for freeing only part of an allocation?

			if (mapping->virtual_address != (uintptr_t)virtual_address || mapping->page_count != page_count) {
				flock_spin_intsafe_unlock(&space->lock);
				return ferr_invalid_argument;
			}

			if (mapping->mapping) {
				// shareable mappings can only be removed via fpage_space_remove_mapping()
				flock_spin_intsafe_unlock(&space->lock);
				return ferr_invalid_argument;
			}

			// unlink the mapping
			if (mapping->next) {
				mapping->next->prev = mapping->prev;
			}
			*mapping->prev = mapping->next;

			break;
		}
	}

	// this will take care of freeing the frames for this mapping;
	// this will also handle the case of having bound-on-demand pages within the mapping (it'll just zero those out).
	fpage_space_flush_mapping_internal(space, virtual_address, page_count, fpage_space_active(space), true, true);

	// ask the buddy allocator to free this in all cases.
	// it'll check if the region is actually part of the buddy allocator's region(s)
	// if so, it'll free it. otherwise, it'll just return.
	fpage_space_free_virtual(space, virtual_address, page_count, false);

	flock_spin_intsafe_unlock(&space->lock);

	if (mapping) {
		FERRO_WUR_IGNORE(fslab_free(&fpage_space_mapping_slab, mapping));
	}

	return ferr_ok;
};

ferr_t fpage_space_map_fixed(fpage_space_t* space, void* physical_address, size_t page_count, void* virtual_address, fpage_flags_t flags) {
	if (physical_address == NULL || page_count == 0 || page_count == SIZE_MAX || virtual_address == NULL) {
		return ferr_invalid_argument;
	}

	// TODO: make sure we don't have a mapping there already

	fpage_prefault_stack(PREFAULT_PAGE_COUNT);
	flock_spin_intsafe_lock(&space->lock);
	fpage_space_map_frame_fixed(space, physical_address, virtual_address, page_count, flags);
	flock_spin_intsafe_unlock(&space->lock);

	return ferr_ok;
};

ferr_t fpage_space_reserve_any(fpage_space_t* space, size_t page_count, void** out_virtual_address) {
	void* virt = NULL;

	if (page_count == 0 || page_count == SIZE_MAX || !out_virtual_address) {
		return ferr_invalid_argument;
	}

	fpage_prefault_stack(PREFAULT_PAGE_COUNT);
	flock_spin_intsafe_lock(&space->lock);
	virt = fpage_space_allocate_virtual(space, page_count, 0, NULL, false);
	flock_spin_intsafe_unlock(&space->lock);

	if (!virt) {
		return ferr_temporary_outage;
	}

	*out_virtual_address = virt;

	return ferr_ok;
};

ferr_t fpage_space_insert_mapping(fpage_space_t* space, fpage_mapping_t* mapping, size_t page_offset, size_t page_count, uint8_t alignment_power, void* virtual_address, fpage_flags_t flags, void** out_virtual_address) {
	ferr_t status = ferr_ok;
	fpage_space_mapping_t* space_mapping = NULL;
	void* alloc_addr = NULL;
	bool release_lock = false;

	if (!out_virtual_address && !virtual_address) {
		status = ferr_invalid_argument;
		goto out;
	}

	status = fpage_mapping_retain(mapping);
	if (status != ferr_ok) {
		mapping = NULL;
		goto out;
	}

	status = fslab_allocate(&fpage_space_mapping_slab, (void*)&space_mapping);
	if (status != ferr_ok) {
		goto out;
	}

	fpage_prefault_stack(PREFAULT_PAGE_COUNT);
	flock_spin_intsafe_lock(&space->lock);
	release_lock = true;

	// if it's in the buddy allocator's region(s), it's reserved for the buddy allocator and can't be mapped for anyone else
	// TODO: allow this to be mapped by allocating it with the buddy allocator
	if (virtual_address && space_region_belongs_to_vmm_allocator(space, virtual_address, page_count)) {
		status = ferr_temporary_outage;
		goto out;
	}

	if (virtual_address) {
		if (!space_region_is_free(space, (uintptr_t)virtual_address, page_count)) {
			status = ferr_temporary_outage;
			goto out;
		}
		alloc_addr = virtual_address;
	} else {
		alloc_addr = fpage_space_allocate_virtual(space, page_count, alignment_power, NULL, false);
		if (!alloc_addr) {
			status = ferr_temporary_outage;
			goto out;
		}
	}

	space_mapping->mapping = mapping;
	space_mapping->virtual_address = (uintptr_t)alloc_addr;
	space_mapping->page_count = page_count;
	space_mapping->page_offset = page_offset;
	space_mapping->flags = flags;

	space_mapping->prev = &space->mappings;
	space_mapping->next = *space_mapping->prev;

	if (space_mapping->next) {
		space_mapping->next->prev = &space_mapping->next;
	}
	*space_mapping->prev = space_mapping;

	// TODO: eagerly map the portions that are already bound.
	//       this method (mapping them as on-demand) does work (it'll fault on each portion and map-in the already-bound portion from the mapping),
	//       but it's not terribly efficient.
	fpage_space_map_frame_fixed(space, (void*)ON_DEMAND_MAGIC, alloc_addr, page_count, flags | fpage_private_flag_inactive | fpage_private_flag_repeat);

	flock_spin_intsafe_unlock(&space->lock);
	release_lock = false;

out:
	if (status == ferr_ok) {
		if (out_virtual_address) {
			*out_virtual_address = alloc_addr;
		}
	} else {
		if (!virtual_address && alloc_addr) {
			fpage_space_free_virtual(space, alloc_addr, page_count, false);
		}
		if (release_lock) {
			flock_spin_intsafe_unlock(&space->lock);
		}
		if (space_mapping) {
			FERRO_WUR_IGNORE(fslab_free(&fpage_space_mapping_slab, space_mapping));
		}
		if (mapping) {
			fpage_mapping_release(mapping);
		}
	}
	return status;
};

ferr_t fpage_space_lookup_mapping(fpage_space_t* space, void* address, bool retain, fpage_mapping_t** out_mapping, size_t* out_page_offset, size_t* out_page_count) {
	ferr_t status = ferr_no_such_resource;

	if (retain && !out_mapping) {
		status = ferr_invalid_argument;
		goto out;
	}

	flock_spin_intsafe_lock(&space->lock);
	for (fpage_space_mapping_t* space_mapping = space->mappings; space_mapping != NULL; space_mapping = space_mapping->next) {
		if (
			space_mapping->mapping &&
			space_mapping->virtual_address <= (uintptr_t)address &&
			space_mapping->virtual_address + (space_mapping->page_count * FPAGE_PAGE_SIZE) > (uintptr_t)address
		) {
			if (retain) {
				// this CANNOT fail
				fpanic_status(fpage_mapping_retain(space_mapping->mapping));
			}
			if (out_mapping) {
				*out_mapping = space_mapping->mapping;
			}
			if (out_page_offset) {
				*out_page_offset = space_mapping->page_offset;
			}
			if (out_page_count) {
				*out_page_count = space_mapping->page_count;
			}
			status = ferr_ok;
			break;
		}
	}
	flock_spin_intsafe_unlock(&space->lock);

out:
	return status;
};

ferr_t fpage_space_remove_mapping(fpage_space_t* space, void* virtual_address) {
	ferr_t status = ferr_ok;
	fpage_space_mapping_t* space_mapping = NULL;

	fpage_prefault_stack(PREFAULT_PAGE_COUNT);
	flock_spin_intsafe_lock(&space->lock);

	for (space_mapping = space->mappings; space_mapping != NULL; space_mapping = space_mapping->next) {
		if (space_mapping->mapping && space_mapping->virtual_address == (uintptr_t)virtual_address) {
			// unlink the mapping
			if (space_mapping->next) {
				space_mapping->next->prev = space_mapping->prev;
			}
			*space_mapping->prev = space_mapping->next;
			break;
		}
	}
	if (!space_mapping) {
		status = ferr_no_such_resource;
	}

	if (status != ferr_ok) {
		flock_spin_intsafe_unlock(&space->lock);
		goto out;
	}

	// now break the mapping in the page tables
	fpage_space_flush_mapping_internal(space, (void*)space_mapping->virtual_address, space_mapping->page_count, fpage_space_active(space), true, false);

	// and free the allocated virtual region
	fpage_space_free_virtual(space, (void*)space_mapping->virtual_address, space_mapping->page_count, false);

	flock_spin_intsafe_unlock(&space->lock);

	// finally, release the mapping and discard the space mapping
	fpage_mapping_release(space_mapping->mapping);
	FERRO_WUR_IGNORE(fslab_free(&fpage_space_mapping_slab, space_mapping));

out:
	return status;
};

ferr_t fpage_space_move_into_mapping(fpage_space_t* space, void* address, size_t page_count, size_t page_offset, fpage_mapping_t* mapping) {
	ferr_t status = ferr_ok;
	fpage_space_mapping_t* space_mapping = NULL;

	flock_spin_intsafe_lock(&space->lock);

	for (space_mapping = space->mappings; space_mapping != NULL; space_mapping = space_mapping->next) {
		if (space_mapping->virtual_address == (uintptr_t)address) {
			if (space_mapping->mapping) {
				// TODO: support binding a mapping to another mapping
				status = ferr_invalid_argument;
				goto out;
			}
			if (space_mapping->page_count != page_count) {
				// TODO: support partially moving a mapping
				status = ferr_invalid_argument;
				goto out;
			}
			break;
		}
	}

	if (!space_mapping) {
		// create a new mapping entry
		status = fslab_allocate(&fpage_space_mapping_slab, (void*)&space_mapping);
		if (status != ferr_ok) {
			goto out;
		}

		space_mapping->prev = &space->mappings;
		space_mapping->next = *space_mapping->prev;

		*space_mapping->prev = space_mapping;
		if (space_mapping->next) {
			space_mapping->next->prev = &space_mapping->next;
		}

		space_mapping->mapping = NULL;
		space_mapping->virtual_address = (uintptr_t)address;
		space_mapping->page_count = page_count;
		space_mapping->page_offset = 0;
		space_mapping->flags = 0; // TODO: update these properly
	}

	fpanic_status(fpage_mapping_retain(mapping));
	if (space_mapping->mapping) {
		fpage_mapping_release(space_mapping->mapping);
	}
	space_mapping->mapping = mapping;
	space_mapping->page_offset = page_offset;

	// FIXME: this is actually wrong; we might have (randomly) gotten two consecutive physical pages
	//        but allocated them separately.

	for (size_t i = 0; i < page_count; ++i) {
		uintptr_t phys = fpage_space_virtual_to_physical(space, (uintptr_t)address + (i * FPAGE_PAGE_SIZE));
		size_t portion_page_count = 0;

		for (; i + portion_page_count < page_count; ++portion_page_count) {
			uintptr_t this_phys = fpage_space_virtual_to_physical(space, (uintptr_t)address + ((i + portion_page_count) * FPAGE_PAGE_SIZE));
			if (this_phys != phys + (portion_page_count * FPAGE_PAGE_SIZE)) {
				break;
			}
		}

		status = fpage_mapping_bind(mapping, page_offset + i, portion_page_count, (void*)phys, fpage_mapping_bind_flag_transfer);
		if (status != ferr_ok) {
			goto out;
		}

		i += portion_page_count - 1;
	}

out:
	flock_spin_intsafe_unlock(&space->lock);
	return status;
};

ferr_t fpage_space_change_permissions(fpage_space_t* space, void* address, size_t page_count, fpage_permissions_t permissions) {
	ferr_t status = ferr_no_such_resource;
	fpage_space_mapping_t* space_mapping = NULL;

	// TODO: allow changing permissions for prebound memory

	flock_spin_intsafe_lock(&space->lock);

	for (space_mapping = space->mappings; space_mapping != NULL; space_mapping = space_mapping->next) {
		if (space_mapping->virtual_address <= (uintptr_t)address && space_mapping->virtual_address + (space_mapping->page_count * FPAGE_PAGE_SIZE) >= (uintptr_t)address + (page_count * FPAGE_PAGE_SIZE)) {
			status = ferr_ok;
			break;
		}
	}

	if (status != ferr_ok) {
		goto out;
	}

	// TODO

	status = ferr_unsupported;

out:
	flock_spin_intsafe_unlock(&space->lock);
	return status;
};

static void fpage_mapping_destroy(fpage_mapping_t* mapping) {
	fpage_mapping_portion_t* next = NULL;

	for (fpage_mapping_portion_t* curr = mapping->portions; curr != NULL; curr = next) {
		next = curr->next;

		if (curr->flags & fpage_mapping_portion_flag_allocated) {
			fpage_pmm_free_frame((void*)curr->physical_address, curr->page_count);
		}

		if (curr->flags & fpage_mapping_portion_flag_backing_mapping) {
			fpage_mapping_release(curr->backing_mapping);
		}

		FERRO_WUR_IGNORE(fslab_free(&fpage_mapping_portion_slab, curr));
	}

	FERRO_WUR_IGNORE(fslab_free(&fpage_mapping_slab, mapping));
};

ferr_t fpage_mapping_retain(fpage_mapping_t* mapping) {
	return frefcount32_increment(&mapping->refcount);
};

void fpage_mapping_release(fpage_mapping_t* mapping) {
	if (frefcount32_decrement(&mapping->refcount) == ferr_permanent_outage) {
		fpage_mapping_destroy(mapping);
	}
};

ferr_t fpage_mapping_new(size_t page_count, fpage_mapping_flags_t flags, fpage_mapping_t** out_mapping) {
	fpage_mapping_t* mapping = NULL;
	ferr_t status = ferr_ok;

	if (page_count > UINT32_MAX) {
		status = ferr_invalid_argument;
		goto out;
	}

	status = fslab_allocate(&fpage_mapping_slab, (void*)&mapping);
	if (status != ferr_ok) {
		goto out;
	}

	flock_spin_intsafe_init(&mapping->lock);
	frefcount32_init(&mapping->refcount);
	mapping->page_count = page_count;
	mapping->portions = NULL;
	mapping->flags = flags;

out:
	if (status == ferr_ok) {
		*out_mapping = mapping;
	} else {
		if (mapping) {
			FERRO_WUR_IGNORE(fslab_free(&fpage_mapping_slab, mapping));
		}
	}
	return status;
};

// this does NOT check if the given portion is already bound
static ferr_t fpage_mapping_bind_locked(fpage_mapping_t* mapping, size_t page_offset, size_t page_count, void* physical_address, fpage_mapping_t* target_mapping, size_t target_mapping_page_offset, fpage_mapping_bind_flags_t flags) {
	ferr_t status = ferr_ok;
	bool free_addr_on_fail = false;
	fpage_mapping_portion_t* new_portion = NULL;

	status = fslab_allocate(&fpage_mapping_portion_slab, (void*)&new_portion);
	if (status != ferr_ok) {
		goto out;
	}

	if (!physical_address) {
		physical_address = fpage_pmm_allocate_frame(page_count, 0, NULL);
		if (!physical_address) {
			status = ferr_temporary_outage;
			goto out;
		}
		free_addr_on_fail = true;

		// if we were asked to zero backing pages, do that now.
		// note that we do NOT zero the backing pages if we're using some given
		// physical pages; we assume the caller wants to insert those backing pages
		// verbatim (e.g. device memory, pre-existing pages, etc.).
		if (mapping->flags & fpage_mapping_flag_zero) {
			simple_memset(map_phys_fixed_offset(physical_address), 0, page_count * FPAGE_PAGE_SIZE);
		}
	}

	// okay, now bind it

	if (target_mapping) {
		new_portion->backing_mapping = target_mapping;
		new_portion->backing_mapping_page_offset = target_mapping_page_offset;
	} else {
		new_portion->physical_address = (uintptr_t)physical_address;
		new_portion->backing_mapping_page_offset = 0;
	}
	new_portion->page_count = page_count;
	new_portion->flags = 0;
	new_portion->virtual_page_offset = page_offset;
	frefcount32_init(&new_portion->refcount);

	if (free_addr_on_fail || (!free_addr_on_fail && (flags & fpage_mapping_bind_flag_transfer) != 0)) {
		new_portion->flags |= fpage_mapping_portion_flag_allocated;
	}

	if (target_mapping) {
		new_portion->flags |= fpage_mapping_portion_flag_backing_mapping;
	}

	// link it into the mapping
	new_portion->prev = &mapping->portions;
	new_portion->next = *new_portion->prev;

	if (new_portion->next) {
		new_portion->next->prev = &new_portion->next;
	}
	*new_portion->prev = new_portion;

out:
	if (status != ferr_ok) {
		if (free_addr_on_fail) {
			fpage_pmm_free_frame(physical_address, page_count);
		}
		if (new_portion) {
			FERRO_WUR_IGNORE(fslab_free(&fpage_mapping_portion_slab, new_portion));
		}
	}
	return status;
};

ferr_t fpage_mapping_bind(fpage_mapping_t* mapping, size_t page_offset, size_t page_count, void* physical_address, fpage_mapping_bind_flags_t flags) {
	ferr_t status = ferr_ok;

	flock_spin_intsafe_lock(&mapping->lock);

	// check if we already have something bound in that region
	for (fpage_mapping_portion_t* portion = mapping->portions; portion != NULL; portion = portion->next) {
		if (portion->virtual_page_offset <= page_offset && portion->virtual_page_offset + portion->page_count >= page_offset + page_count) {
			// this portion overlaps with the target region
			status = ferr_already_in_progress;
			goto out;
		}
	}

	status = fpage_mapping_bind_locked(mapping, page_offset, page_count, physical_address, NULL, 0, flags);

out:
	flock_spin_intsafe_unlock(&mapping->lock);
	return status;
};

ferr_t fpage_mapping_bind_indirect(fpage_mapping_t* mapping, size_t page_offset, size_t page_count, fpage_mapping_t* target_mapping, size_t target_mapping_page_offset, fpage_mapping_bind_flags_t flags) {
	ferr_t status = ferr_ok;

	status = fpage_mapping_retain(target_mapping);
	if (status != ferr_ok) {
		target_mapping = NULL;
		goto out_unlocked;
	}

	flock_spin_intsafe_lock(&mapping->lock);

	// check if we already have something bound in that region
	for (fpage_mapping_portion_t* portion = mapping->portions; portion != NULL; portion = portion->next) {
		if (portion->virtual_page_offset <= page_offset && portion->virtual_page_offset + portion->page_count >= page_offset + page_count) {
			// this portion overlaps with the target region
			status = ferr_already_in_progress;
			goto out;
		}
	}

	status = fpage_mapping_bind_locked(mapping, page_offset, page_count, NULL, target_mapping, target_mapping_page_offset, flags);

out:
	flock_spin_intsafe_unlock(&mapping->lock);
out_unlocked:
	if (status != ferr_ok) {
		if (target_mapping) {
			fpage_mapping_release(target_mapping);
		}
	}
	return status;
};

ferr_t fpage_mapping_page_count(fpage_mapping_t* mapping, size_t* out_page_count) {
	*out_page_count = mapping->page_count;
	return ferr_ok;
};

//
// page faults
//

// must be holding the L4 table lock
static void space_refresh_mapping(fpage_space_t* space, uintptr_t virtual_address, size_t page_count) {
	uint16_t l4_index = FPAGE_VIRT_L4(virtual_address);
	fpage_table_t* space_phys_table = space->l4_table;
	fpage_table_t* space_table = map_phys_fixed_offset(space_phys_table);
	uint64_t space_entry = space_table->entries[l4_index];
	uint64_t current_entry = fpage_table_load(1, l4_index, 0, 0, 0);

	if (space_entry != current_entry) {
		fpage_table_store(1, l4_index, 0, 0, 0, space_entry);

		if (fpage_entry_is_active(current_entry)) {
			// we need to flush the entire table
			// FIXME: the precise table flush (`fpage_flush_table()`) doesn't work properly, so we do a full table flush
			fpage_invalidate_tlb_for_active_space();
		}
	}

	// flush this mapping
	fpage_space_flush_mapping_internal(space, (void*)virtual_address, page_count, true, false, false);
};

// must be holding the L4 table lock
FERRO_NO_KASAN
static bool address_is_bound_on_demand(fpage_space_t* space, void* address) {
	uint16_t l4 = FPAGE_VIRT_L4(address);
	uint16_t l3 = FPAGE_VIRT_L3(address);
	uint16_t l2 = FPAGE_VIRT_L2(address);
	uint16_t l1 = FPAGE_VIRT_L1(address);

	fpage_table_t* table = NULL;
	uint64_t entry = 0;

	if (space) {
		table = map_phys_fixed_offset(space->l4_table);
		entry = table->entries[l4];
	} else {
		entry = fpage_table_load(1, l4, 0, 0, 0);
	}

	// check if L4 is active
	if (!fpage_entry_is_active(entry)) {
		return false;
	}

	// at L4, large pages are not allowed, so no need to check

	table = map_phys_fixed_offset((void*)fpage_entry_address(entry));
	entry = table->entries[l3];

	// check if L3 is active
	if (!fpage_entry_is_active(entry)) {
		return fpage_entry_address(entry) == ON_DEMAND_MAGIC;
	}

	// at L3, there might be a very large page instead of a table
	if (fpage_entry_is_large_page_entry(entry)) {
		return false;
	}

	table = map_phys_fixed_offset((void*)fpage_entry_address(entry));
	entry = table->entries[l2];

	// check if L2 is active
	if (!fpage_entry_is_active(entry)) {
		return fpage_entry_address(entry) == ON_DEMAND_MAGIC;
	}

	// at L2, there might be a large page instead of a table
	if (fpage_entry_is_large_page_entry(entry)) {
		return false;
	}

	table = map_phys_fixed_offset((void*)fpage_entry_address(entry));
	entry = table->entries[l1];

	// check if L1 is active
	if (!fpage_entry_is_active(entry)) {
		return fpage_entry_address(entry) == ON_DEMAND_MAGIC;
	}

	return false;
};

// must NOT be holding the L4 table lock, the mappings head lock, nor any of the mapping locks
static bool try_handling_fault_with_space(uintptr_t faulting_address, fpage_space_t* space) {
	uintptr_t faulting_page = fpage_round_down_page(faulting_address);

retry:
	// no need to prefault; the stack for the page fault handler should be prebound
	flock_spin_intsafe_lock(&space->lock);

	if (fpage_space_virtual_to_physical(space, faulting_address) != UINTPTR_MAX) {
		// this address was actually already mapped (likely by another CPU),
		// it's just that it wasn't present in the current CPU's root table.
		// just go ahead and update our mapping
		space_refresh_mapping(space, faulting_address, 1);
		flock_spin_intsafe_unlock(&space->lock);
		return true;
	}

	if (address_is_bound_on_demand(space, (void*)faulting_address)) {
		// try to bind it now

		fpage_space_mapping_t space_mapping_copy;
		void* phys_addr = NULL;
		uint32_t page_offset;
		bool found = false;

		found = false;

		for (fpage_space_mapping_t* space_mapping = space->mappings; space_mapping != NULL; space_mapping = space_mapping->next) {
			if (
				space_mapping->virtual_address <= faulting_address &&
				space_mapping->virtual_address + (space_mapping->page_count * FPAGE_PAGE_SIZE) > faulting_address
			) {
				if (space_mapping->mapping) {
					// this CANNOT fail
					fpanic_status(fpage_mapping_retain(space_mapping->mapping));
				}
				simple_memcpy(&space_mapping_copy, space_mapping, sizeof(space_mapping_copy));
				found = true;
				break;
			}
		}

		if (!found) {
			// the address wasn't actually mapped
			flock_spin_intsafe_unlock(&space->lock);
			return false;
		}

retry_bound:
		page_offset = space_mapping_copy.page_offset + fpage_round_down_to_page_count(faulting_page - space_mapping_copy.virtual_address);

		if (space_mapping_copy.mapping) {
			fpage_mapping_t* target_mapping = space_mapping_copy.mapping;

			flock_spin_intsafe_unlock(&space->lock);
			flock_spin_intsafe_lock(&target_mapping->lock);

			// see if any of the existing portions satisfy this address
			for (fpage_mapping_portion_t* portion = target_mapping->portions; portion != NULL; portion = portion->next) {
				if (portion->virtual_page_offset <= page_offset && portion->virtual_page_offset + portion->page_count > page_offset) {
					// this portion satisfies the requested address
					if (portion->flags & fpage_mapping_portion_flag_backing_mapping) {
						// this portion is actually backed up by another mapping
						// let's check that mapping now
						//
						// FIXME: by the time we actually get around to checking the backing mapping,
						//        someone may have unmapped it from the original target mapping portion,
						//        since we don't hold the original target mapping lock while checking the
						//        secondary target mapping. this issue isn't possible with the first level
						//        of indirection (since we check that the original mapping in the space is
						//        the same), but for any level of indirection greater than 1, this is possible.
						fpage_mapping_t* mapping = portion->backing_mapping;
						fpanic_status(fpage_mapping_retain(mapping));
						page_offset = (page_offset - portion->virtual_page_offset) + portion->backing_mapping_page_offset;
						flock_spin_intsafe_unlock(&target_mapping->lock);
						fpage_mapping_release(target_mapping);
						target_mapping = mapping;
						goto retry;
					} else {
						phys_addr = (void*)(portion->physical_address + (page_offset - portion->virtual_page_offset) * FPAGE_PAGE_SIZE);
					}
					break;
				}
			}

			if (!phys_addr) {
				// none of the portions satisfied the request;
				// let's see if we can try binding an additional portion
				if (fpage_mapping_bind_locked(target_mapping, page_offset, 1, NULL, NULL, 0, 0) != ferr_ok) {
					// we failed to bind this portion;
					// go ahead and fault
					flock_spin_intsafe_unlock(&target_mapping->lock);
					fpage_mapping_release(target_mapping);
					return false;
				}

				// we still hold the lock here, so we know that the portion that was just added to the head of the portions linked list
				// is the portion we want to use
				phys_addr = (void*)(target_mapping->portions->physical_address + (page_offset - target_mapping->portions->virtual_page_offset) * FPAGE_PAGE_SIZE);
			}

			flock_spin_intsafe_unlock(&target_mapping->lock);

			flock_spin_intsafe_lock(&space->lock);

			// we had to drop the lock, so someone might've removed the mapping we had.
			// let's see if we can find it again.

			// go ahead and release the extra reference we acquired;
			// the address space can't release it's reference on it as long as we hold the mappings lock
			fpage_mapping_release(target_mapping);

			// we actually have to first check if it's still unmapped (since, again, we dropped the lock)
			if (fpage_space_virtual_to_physical(space, faulting_address) != UINTPTR_MAX) {
				// just go ahead and update our mapping
				space_refresh_mapping(space, faulting_address, 1);
				flock_spin_intsafe_unlock(&space->lock);
				return true;
			}

			found = false;

			if (address_is_bound_on_demand(space, (void*)faulting_address)) {
				for (fpage_space_mapping_t* space_mapping = space->mappings; space_mapping != NULL; space_mapping = space_mapping->next) {
					if (
						space_mapping->virtual_address <= faulting_address &&
						space_mapping->virtual_address + (space_mapping->page_count * FPAGE_PAGE_SIZE) > faulting_address
					) {
						// okay, we've found a mapping for the address again.
						// let's see if it's the same one

						if (simple_memcmp(space_mapping, &space_mapping_copy, sizeof(space_mapping_copy)) == 0) {
							// great, they're the same mapping!
							found = true;
							break;
						} else {
							// oh, the mapping has changed.
							// let's re-evaluate everything with this "new" mapping
							phys_addr = NULL;
							found = false;

							if (space_mapping->mapping) {
								// this CANNOT fail
								fpanic_status(fpage_mapping_retain(space_mapping->mapping));
							}
							simple_memcpy(&space_mapping_copy, space_mapping, sizeof(space_mapping_copy));

							goto retry_bound;
						}
					}
				}
			}

			if (!found) {
				// the address is no longer mapped
				flock_spin_intsafe_unlock(&space->lock);
				return false;
			}
		} else {
			// this is a non-shared bound-on-demand page;
			// just allocate a frame

			phys_addr = fpage_pmm_allocate_frame(1, 0, NULL);

			if (!phys_addr) {
				// not enough memory to bind it
				flock_spin_intsafe_unlock(&space->lock);
				return false;
			}

			if (space_mapping_copy.flags & fpage_flag_zero) {
				// zero out the new page
				simple_memset(map_phys_fixed_offset(phys_addr), 0, FPAGE_PAGE_SIZE);
			}
		}

		// okay, we've got a valid phys_addr here that we're going to map
		fpage_space_map_frame_fixed(space, phys_addr, (void*)faulting_page, 1, space_mapping_copy.flags);

		flock_spin_intsafe_unlock(&space->lock);

		return true;
	}

	flock_spin_intsafe_unlock(&space->lock);

	return false;
};

static void page_fault_handler(void* context) {
	uintptr_t faulting_address = fpage_fault_address();
	uintptr_t faulting_page = fpage_round_down_page(faulting_address);
	fpage_space_t* space = fpage_space_current();

#if FPAGE_DEBUG_LOG_FAULTS
	fconsole_logf("Handling fault for %p\n", (void*)faulting_address);
#endif

#if FERRO_KASAN
	if (faulting_page >= FERRO_KASAN_SHADOW_BASE && faulting_page < FERRO_KASAN_SHADOW_BASE + FPAGE_SUPER_LARGE_PAGE_SIZE) {
		// short-circuit: this is part of the KASan shadow; let's map it
		void* frame = fpage_pmm_allocate_frame(1, 0, NULL);
		if (!frame) {
			fpanic("Failed to allocate frame for KASan shadow");
		}
		fpage_space_map_frame_fixed(space, frame, (void*)faulting_page, 1, 0);
		ferro_kasan_fill_unchecked((void*)faulting_page, 0xff, FPAGE_PAGE_SIZE);
		return;
	}
#endif

	// TODO: suspend threads while we update their address spaces
	//       when we need to do more time-consuming work (like swapping, CoW, etc.).
	//       binding on-demand is fine to do in the interrupt handler, though.
	//       this should be pretty quick in practice.

	if (try_handling_fault_with_space(faulting_address, space)) {
		// we've successfully mapped it; exit the interrupt
		return;
	}

	// if the current address space is not the kernel address space, try handling
	// it with that one; the kernel address space is always active.

	if (space != fpage_space_kernel() && try_handling_fault_with_space(faulting_address, fpage_space_kernel())) {
		// we've successfully mapped it; exit the interrupt
		return;
	}

	// try to see if the current thread can handle it
	if (fint_current_frame() == fint_root_frame(fint_current_frame()) && FARCH_PER_CPU(current_thread)) {
		fthread_t* thread = FARCH_PER_CPU(current_thread);
		fthread_private_t* private_thread = (void*)thread;
		bool handled = false;
		uint8_t hooks_in_use;

		flock_spin_intsafe_lock(&thread->lock);
		hooks_in_use = private_thread->hooks_in_use;
		flock_spin_intsafe_unlock(&thread->lock);

		for (uint8_t slot = 0; slot < sizeof(private_thread->hooks) / sizeof(*private_thread->hooks); ++slot) {
			if ((hooks_in_use & (1 << slot)) == 0) {
				continue;
			}

			if (!private_thread->hooks[slot].page_fault) {
				continue;
			}

			ferr_t hook_status = private_thread->hooks[slot].page_fault(private_thread->hooks[slot].context, thread, (void*)faulting_address);

			if (hook_status == ferr_ok || hook_status == ferr_permanent_outage) {
				handled = true;
			}

			if (hook_status == ferr_permanent_outage) {
				break;
			}
		}

		if (handled) {
			return;
		}
	}

	// okay, let's give up

	fconsole_logf("Faulted on %p\n", (void*)faulting_address);
	fint_log_frame(fint_current_frame());
	fint_trace_interrupted_stack(fint_current_frame());
	fpanic("Faulted on %p", (void*)faulting_address);
};

void fpage_init_secondary_cpu(void) {
	// nothing for now
};

typedef bool (*fpage_space_table_iterator_f)(void* context, fpage_space_t* space, void* virtual_address, size_t page_count);

void fpage_space_iterate_table(fpage_space_t* space, fpage_space_table_iterator_f iterator, void* context) {
	// this looks horrible (theoretically 256*512^3 iterations or about 34.3 billion iterations)
	// but it's actually fine because there's no way we would ever use that much memory
	for (uint16_t l4 = 0; l4 < FPAGE_USER_L4_MAX; ++l4) {
		fpage_table_t* root_table = map_phys_fixed_offset(space->l4_table);
		uint64_t l4_entry = root_table->entries[l4];

		if (!fpage_entry_is_active(l4_entry)) {
			continue;
		}

		for (uint16_t l3 = 0; l3 < 512; ++l3) {
			fpage_table_t* l4_table = map_phys_fixed_offset((void*)fpage_entry_address(l4_entry));
			uint64_t l3_entry = l4_table->entries[l3];

			if (!fpage_entry_is_active(l3_entry)) {
				continue;
			}

			if (fpage_entry_is_large_page_entry(l3_entry)) {
				if (!iterator(context, space, (void*)fpage_make_virtual_address(l4, l3, 0, 0, 0), FPAGE_VERY_LARGE_PAGE_COUNT)) {
					return;
				}
				continue;
			}

			for (uint16_t l2 = 0; l2 < 512; ++l2) {
				fpage_table_t* l3_table = map_phys_fixed_offset((void*)fpage_entry_address(l3_entry));
				uint64_t l2_entry = l3_table->entries[l2];

				if (!fpage_entry_is_active(l2_entry)) {
					continue;
				}

				if (fpage_entry_is_large_page_entry(l2_entry)) {
					if (!iterator(context, space, (void*)fpage_make_virtual_address(l4, l3, l2, 0, 0), FPAGE_LARGE_PAGE_COUNT)) {
						return;
					}
					continue;
				}

				for (uint16_t l1 = 0; l1 < 512; ++l1) {
					fpage_table_t* l2_table = map_phys_fixed_offset((void*)fpage_entry_address(l2_entry));
					uint64_t l1_entry = l2_table->entries[l1];

					if (!fpage_entry_is_active(l1_entry)) {
						continue;
					}

					if (!iterator(context, space, (void*)fpage_make_virtual_address(l4, l3, l2, l1, 0), 1)) {
						return;
					}
				}
			}
		}
	}
};

// DEBUGGING
FERRO_STRUCT(fpage_space_find_first_physical_iterator_context) {
	void* virt;
	void* phys;
};
static bool fpage_space_find_first_physical_iterator(void* _context, fpage_space_t* space, void* virtual_address, size_t page_count) {
	fpage_space_find_first_physical_iterator_context_t* context = _context;

	if (fpage_space_virtual_to_physical(space, (uintptr_t)virtual_address) == (uintptr_t)context->phys) {
		context->virt = virtual_address;
		return false;
	}

	return true;
};
void* fpage_space_find_first_physical(fpage_space_t* space, void* physical_address) {
	fpage_space_find_first_physical_iterator_context_t context;
	context.virt = NULL;
	context.phys = physical_address;
	fpage_space_iterate_table(space, fpage_space_find_first_physical_iterator, &context);
	return context.virt;
};
