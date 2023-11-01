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

#include <ferro/core/paging.private.h>
#include <ferro/core/locks.h>
#include <ferro/bits.h>
#include <ferro/core/panic.h>
#include <libsimple/libsimple.h>
#include <ferro/core/mempool.h>
#include <stdatomic.h>
#include <ferro/core/interrupts.h>
#include <ferro/core/console.h>
#include <ferro/core/mempool.h>
#include <ferro/core/threads.private.h>
#include <ferro/core/mm.private.h>
#include <ferro/mm/slab.h>

#ifndef FPAGE_VMM_CHECK_FREE
	#define FPAGE_VMM_CHECK_FREE 0
#endif

#ifndef FPAGE_VMM_CLEAR_ON_REMOVE
	#define FPAGE_VMM_CLEAR_ON_REMOVE 0
#endif

/**
 * @file
 *
 * Virtual memory allocation.
 */

fpage_space_t fpage_vmm_kernel_address_space = {
	.l4_table = NULL,
	.lock = FLOCK_SPIN_INTSAFE_INIT,
	.blocks = NULL,
	.vmm_allocator_start = FERRO_KERNEL_VIRTUAL_START,
	.vmm_allocator_page_count = ((UINT64_MAX - FERRO_KERNEL_VIRTUAL_START) + 1) / FPAGE_PAGE_SIZE,
	.space_destruction_waiters = FWAITQ_INIT,
	.mappings = NULL,
};

FERRO_NO_KASAN
static bool space_ensure_table(fpage_space_t* space, fpage_table_t* phys_parent, size_t index, fpage_table_t** out_phys_child, bool kasan) {
	fpage_table_t* parent = map_phys_fixed_offset(phys_parent);
	if (!fpage_entry_is_active(parent->entries[index])) {
#if FERRO_KASAN
		fpage_table_t* table = fpage_pmm_allocate_frame(fpage_round_up_page(sizeof(fpage_table_t)) / FPAGE_PAGE_SIZE, 0, kasan ? &fpage_map_kasan_pmm_allocate_marker : NULL);
#else
		fpage_table_t* table = fpage_pmm_allocate_frame(fpage_round_up_page(sizeof(fpage_table_t)) / FPAGE_PAGE_SIZE, 0, NULL);
#endif

		if (!table) {
			// oh no, looks like we don't have any more memory!
			return false;
		}

		ferro_kasan_fill_unchecked(map_phys_fixed_offset(table), 0, fpage_round_up_page(sizeof(fpage_table_t)));

		parent = map_phys_fixed_offset(phys_parent);

		// table entries are marked as unprivileged; this is so that both privileged and unprivileged pages contained within them can be access properly.
		// the final entries (e.g. large page entries or L1 page table entries) should be marked with whatever privilege level they need.
		parent->entries[index] = fpage_entry_mark_privileged(fpage_table_entry((uintptr_t)table, true), false);
		fpage_synchronize_after_table_modification();

		if (out_phys_child) {
			*out_phys_child = table;
		}

		if (fpage_space_active(space) && phys_parent == space->l4_table) {
			// the address space is active and this is a new entry in the root table, so we need to mirror it in the root system table
			fpage_vmm_root_table->entries[index] = parent->entries[index];
		}
	} else {
		if (out_phys_child) {
			*out_phys_child = (void*)fpage_entry_address(parent->entries[index]);
		}
	}

	return true;
};

// *should* be holding the L4 table lock
FERRO_NO_KASAN
uintptr_t fpage_space_virtual_to_physical(fpage_space_t* space, uintptr_t virtual_address) {
	if (!fpage_address_is_canonical(virtual_address)) {
		return UINTPTR_MAX;
	}

	uint16_t l4 = FPAGE_VIRT_L4(virtual_address);
	uint16_t l3 = FPAGE_VIRT_L3(virtual_address);
	uint16_t l2 = FPAGE_VIRT_L2(virtual_address);
	uint16_t l1 = FPAGE_VIRT_L1(virtual_address);
	uint16_t offset = FPAGE_VIRT_OFFSET(virtual_address);
	fpage_table_t* table = NULL;
	uint64_t entry = 0;

	if (!space) {
		return UINTPTR_MAX;
	}

	table = map_phys_fixed_offset(space->l4_table);
	entry = table->entries[l4];

	// L4 table

	if (!fpage_entry_is_active(entry)) {
		return UINTPTR_MAX;
	}

	table = map_phys_fixed_offset((void*)fpage_entry_address(entry));
	entry = table->entries[l3];

	// L3 table

	if (!fpage_entry_is_active(entry)) {
		return UINTPTR_MAX;
	}

	if (fpage_entry_is_large_page_entry(entry)) {
		return fpage_entry_address(entry) | FPAGE_VIRT_VERY_LARGE_OFFSET(virtual_address);
	}

	table = map_phys_fixed_offset((void*)fpage_entry_address(entry));
	entry = table->entries[l2];

	// L2 table

	if (!fpage_entry_is_active(entry)) {
		return UINTPTR_MAX;
	}

	if (fpage_entry_is_large_page_entry(entry)) {
		return fpage_entry_address(entry) | FPAGE_VIRT_LARGE_OFFSET(virtual_address);
	}

	table = map_phys_fixed_offset((void*)fpage_entry_address(entry));
	entry = table->entries[l1];

	// L1 table

	if (!fpage_entry_is_active(entry)) {
		return UINTPTR_MAX;
	}

	return fpage_entry_address(entry) | offset;
};

/**
 * Temporarily maps a virtual address from an address space such that
 * it can be temporarily accessed without the address space being active.
 *
 * Like map_phys_fixed_offset(), addresses returned by calls to this function should not
 * be assumed to remain valid past most function calls. Only a select few known not to request temporary mappings
 * can be called without needing to remap temporarily-mapped addresses afterwards.
 */
FERRO_ALWAYS_INLINE void* space_map_phys_fixed_offset(fpage_space_t* space, void* virt) {
	uintptr_t phys = fpage_space_virtual_to_physical(space, (uintptr_t)virt);
	if (phys == UINTPTR_MAX) {
		fpanic("bad address within space");
		return NULL;
	}
	return map_phys_fixed_offset((void*)phys);
};

#define space_map_phys_fixed_offset_type(space, virt) ((__typeof__((virt)))space_map_phys_fixed_offset((space), (virt)))

// must be holding the L4 table lock
static void space_free_table(fpage_space_t* space, fpage_table_t* table) {
	fpage_pmm_free_frame((void*)fpage_space_virtual_to_physical(space, (uintptr_t)table), fpage_round_up_page(sizeof(fpage_table_t)) / FPAGE_PAGE_SIZE);
};

// must be holding the L4 table lock if modifying a table within an address space
static void break_entry(size_t levels, size_t l4_index, size_t l3_index, size_t l2_index, size_t l1_index) {
	uintptr_t start_addr = fpage_make_virtual_address((levels > 0) ? l4_index : 0, (levels > 1) ? l3_index : 0, (levels > 2) ? l2_index : 0, (levels > 3) ? l1_index : 0, 0);
	uintptr_t end_addr = fpage_make_virtual_address((levels > 0) ? l4_index : TABLE_ENTRY_COUNT - 1, (levels > 1) ? l3_index : TABLE_ENTRY_COUNT - 1, (levels > 2) ? l2_index : TABLE_ENTRY_COUNT - 1, (levels > 3) ? l1_index : TABLE_ENTRY_COUNT - 1, 0xfff) + 1;

	// first, invalidate the entry
	if (levels == 0) {
		// invalidating the L4 table would be A Bad Thing (TM)
	} else {
		fpage_table_store(levels, l4_index, l3_index, l2_index, l1_index, 0);
		fpage_synchronize_after_table_modification();
	}

	// now invalidate TLB entries for all the addresses
	fpage_invalidate_tlb_for_range((void*)start_addr, (void*)end_addr);
	fpage_synchronize_after_table_modification();
};

bool fpage_space_map_frame_fixed_debug_iterator(void* context, uintptr_t virtual_address, uintptr_t physical_address, uint64_t page_count) {
	uintptr_t check_addr = (uintptr_t)context;

	if (
		(FPAGE_VIRT_L4(virtual_address) == fpage_root_offset_index && page_count >= FPAGE_VERY_LARGE_PAGE_COUNT) ||
		FPAGE_VIRT_L4(virtual_address) == fpage_root_recursive_index ||
		FPAGE_VIRT_L4(virtual_address) == FPAGE_VIRT_L4(FERRO_KASAN_SHADOW_BASE)
	) {
		return true;
	}

	if (check_addr >= physical_address && check_addr < physical_address + (page_count * FPAGE_PAGE_SIZE)) {
		fpanic("Trying to map physical frame in-use by the kernel into a user address space!");
	}

	return true;
};

// NOTE: this function ***WILL*** overwrite existing entries!
// must be holding L4 table lock
FERRO_NO_KASAN
void fpage_space_map_frame_fixed(fpage_space_t* space, void* phys_frame, void* virt_frame, size_t page_count, fpage_private_flags_t flags) {
	uintptr_t physical_frame = (uintptr_t)phys_frame;
	uintptr_t virtual_frame = (uintptr_t)virt_frame;
	bool no_cache = (flags & fpage_flag_no_cache) != 0;
	bool unprivileged = (flags & fpage_flag_unprivileged) != 0;
	bool inactive = (flags & fpage_private_flag_inactive) != 0;
	bool repeat = (flags & fpage_private_flag_repeat) != 0;
	bool kasan = (flags & fpage_private_flag_kasan) != 0;

	size_t orig_page_count = page_count;

	while (page_count > 0) {
		size_t l4_index = FPAGE_VIRT_L4(virtual_frame);
		size_t l3_index = FPAGE_VIRT_L3(virtual_frame);
		size_t l2_index = FPAGE_VIRT_L2(virtual_frame);
		size_t l1_index = FPAGE_VIRT_L1(virtual_frame);

		// L4 table

		fpage_table_t* phys_table = space->l4_table;
		fpage_table_t* table = map_phys_fixed_offset(phys_table);
		uint64_t entry = table->entries[l4_index];

		if (!space_ensure_table(space, phys_table, l4_index, &phys_table, kasan)) {
			return;
		}

		// L3 table

		table = map_phys_fixed_offset(phys_table);
		entry = table->entries[l3_index];

		if (fpage_is_very_large_page_aligned(physical_frame) && fpage_is_very_large_page_aligned(virtual_frame) && page_count >= FPAGE_VERY_LARGE_PAGE_COUNT) {
			if (!fpage_entry_is_large_page_entry(entry)) {
				// TODO: this doesn't free subtables
				space_free_table(space, (void*)fpage_entry_address(entry));
			}

			// break the existing entry
			if (fpage_space_active(space)) {
				break_entry(2, l4_index, l3_index, 0, 0);

				{
					uintptr_t start_flush = fpage_make_virtual_address(l4_index, l3_index, 0, 0, 0);
					uintptr_t end_flush = start_flush + FPAGE_VERY_LARGE_PAGE_SIZE;
					fpage_invalidate_tlb_for_range((void*)start_flush, (void*)end_flush);
				}
			}

			// now map our entry
			table = map_phys_fixed_offset(phys_table);
			entry = fpage_very_large_page_entry(physical_frame, true);
			if (no_cache) {
				entry = fpage_entry_disable_caching(entry);
			}
			if (unprivileged) {
				entry = fpage_entry_mark_privileged(entry, false);
			}
			if (inactive) {
				entry = fpage_entry_mark_active(entry, false);
			}
			table->entries[l3_index] = entry;
			fpage_synchronize_after_table_modification();

			page_count -= FPAGE_VERY_LARGE_PAGE_COUNT;
			if (!repeat) {
				physical_frame += FPAGE_VERY_LARGE_PAGE_SIZE;
			}
			virtual_frame += FPAGE_VERY_LARGE_PAGE_SIZE;

			continue;
		}

		if (fpage_entry_is_large_page_entry(entry) && fpage_space_active(space)) {
			break_entry(2, l4_index, l3_index, 0, 0);

			{
				uintptr_t start_flush = fpage_make_virtual_address(l4_index, l3_index, 0, 0, 0);
				uintptr_t end_flush = start_flush + FPAGE_VERY_LARGE_PAGE_SIZE;
				fpage_invalidate_tlb_for_range((void*)start_flush, (void*)end_flush);
			}

			// NOTE: this does not currently handle the case of partially remapping a large page
			//       e.g. if we want to map the first half to another location but keep the last half to where the large page pointed
			//       however, this is probably not something we'll ever want or need to do, so it's okay for now.
			//       just be aware of this limitation present here.
		}

		if (!space_ensure_table(space, phys_table, l3_index, &phys_table, kasan)) {
			return;
		}

		// L2 table

		table = map_phys_fixed_offset(phys_table);
		entry = table->entries[l2_index];

		if (fpage_is_large_page_aligned(physical_frame) && fpage_is_large_page_aligned(virtual_frame) && page_count >= FPAGE_LARGE_PAGE_COUNT) {
			if (!fpage_entry_is_large_page_entry(entry)) {
				// TODO: this doesn't free subtables
				space_free_table(space, (void*)fpage_entry_address(entry));
			}

			// break the existing entry
			if (fpage_space_active(space)) {
				break_entry(3, l4_index, l3_index, l2_index, 0);

				{
					uintptr_t start_flush = fpage_make_virtual_address(l4_index, l3_index, l2_index, 0, 0);
					uintptr_t end_flush = start_flush + FPAGE_LARGE_PAGE_SIZE;
					fpage_invalidate_tlb_for_range((void*)start_flush, (void*)end_flush);
				}
			}

			// now map our entry
			table = map_phys_fixed_offset(phys_table);
			entry = fpage_large_page_entry(physical_frame, true);
			if (no_cache) {
				entry = fpage_entry_disable_caching(entry);
			}
			if (unprivileged) {
				entry = fpage_entry_mark_privileged(entry, false);
			}
			if (inactive) {
				entry = fpage_entry_mark_active(entry, false);
			}
			table->entries[l2_index] = entry;
			fpage_synchronize_after_table_modification();

			page_count -= FPAGE_LARGE_PAGE_COUNT;
			if (!repeat) {
				physical_frame += FPAGE_LARGE_PAGE_SIZE;
			}
			virtual_frame += FPAGE_LARGE_PAGE_SIZE;

			continue;
		}

		if (fpage_entry_is_large_page_entry(entry) && fpage_space_active(space)) {
			break_entry(3, l4_index, l3_index, l2_index, 0);

			{
				uintptr_t start_flush = fpage_make_virtual_address(l4_index, l3_index, l2_index, 0, 0);
				uintptr_t end_flush = start_flush + FPAGE_LARGE_PAGE_SIZE;
				fpage_invalidate_tlb_for_range((void*)start_flush, (void*)end_flush);
			}

			// same note as for the l3 large page case
		}

		if (!space_ensure_table(space, phys_table, l2_index, &phys_table, kasan)) {
			return;
		}

		// L1 table

		table = map_phys_fixed_offset(phys_table);
		entry = table->entries[l1_index];

		if (entry && fpage_space_active(space)) {
			break_entry(4, l4_index, l3_index, l2_index, l1_index);

			{
				uintptr_t start_flush = fpage_make_virtual_address(l4_index, l3_index, l2_index, l1_index, 0);
				uintptr_t end_flush = start_flush + FPAGE_PAGE_SIZE;
				fpage_invalidate_tlb_for_range((void*)start_flush, (void*)end_flush);
			}
		}

		table = map_phys_fixed_offset(phys_table);
		entry = fpage_page_entry(physical_frame, true);
		if (no_cache) {
			entry = fpage_entry_disable_caching(entry);
		}
		if (unprivileged) {
			entry = fpage_entry_mark_privileged(entry, false);
		}
		if (inactive) {
			entry = fpage_entry_mark_active(entry, false);
		}
		table->entries[l1_index] = entry;
		fpage_synchronize_after_table_modification();

		page_count -= 1;
		if (!repeat) {
			physical_frame += FPAGE_PAGE_SIZE;
		}
		virtual_frame += FPAGE_PAGE_SIZE;
	}
};

// must be holding the space lock
FERRO_NO_KASAN
static void space_insert_virtual_free_block(fpage_space_t* space, fpage_free_block_t* space_block, size_t block_page_count) {
	fpage_free_block_t* the_phys_block = fpage_pmm_allocate_frame(fpage_round_up_page(sizeof(fpage_free_block_t)) / FPAGE_PAGE_SIZE, 0, NULL);
	fpage_free_block_t* block = NULL;
	fpage_free_block_t** block_prev = NULL;
	fpage_free_block_t* block_next = space->blocks;

	if (!the_phys_block) {
		fpanic("failed to allocate physical block for virtual free block");
	}

	fpage_space_map_frame_fixed(space, the_phys_block, space_block, fpage_round_up_page(sizeof(fpage_free_block_t)) / FPAGE_PAGE_SIZE, 0);

	block = space_map_phys_fixed_offset(space, space_block);

	while (block_next && block_next < space_block) {
		block_prev = &block_next->next;
		block_next = *space_map_phys_fixed_offset_type(space, block_prev);
	}

	block = space_map_phys_fixed_offset_type(space, space_block);
	block->prev = block_prev;
	block->next = block_next;
	block->page_count = block_page_count;

	if (block_prev) {
		*space_map_phys_fixed_offset_type(space, block_prev) = space_block;
	} else {
		space->blocks = space_block;
	}

	if (block_next) {
		space_map_phys_fixed_offset_type(space, block_next)->prev = &space_block->next;
	}
};

// must be holding the space lock
FERRO_NO_KASAN
static void space_remove_virtual_free_block(fpage_space_t* space, fpage_free_block_t* space_block) {
	fpage_free_block_t* block = space_map_phys_fixed_offset(space, space_block);

	if (block->prev) {
		*space_map_phys_fixed_offset_type(space, block->prev) = block->next;
	} else {
		space->blocks = block->next;
	}

	if (block->next) {
		space_map_phys_fixed_offset_type(space, block->next)->prev = block->prev;
	}

#if FPAGE_VMM_CLEAR_ON_REMOVE
	block->prev = NULL;
	block->next = NULL;
	block->page_count = 0;
#endif

	fpage_pmm_free_frame((void*)fpage_space_virtual_to_physical(space, (uintptr_t)space_block), fpage_round_up_page(sizeof(fpage_free_block_t)) / FPAGE_PAGE_SIZE);

	fpage_space_flush_mapping_internal(space, space_block, 1, fpage_space_active(space), true, false);
};

static fpage_free_block_t* space_merge_free_blocks(fpage_space_t* space, fpage_free_block_t* space_block) {
	fpage_free_block_t* block = space_map_phys_fixed_offset_type(space, space_block);
	uint64_t curr_page_count = block->page_count;
	uint64_t byte_size = curr_page_count * FPAGE_PAGE_SIZE;
	fpage_free_block_t* space_block_end = (fpage_free_block_t*)((uintptr_t)space_block + byte_size);

	if (block->next) {
		if (block->next == space_block_end) {
			uint64_t next_page_count = space_map_phys_fixed_offset_type(space, block->next)->page_count;
			space_remove_virtual_free_block(space, block->next);
			block->page_count += next_page_count;
			return space_block;
		}
	}

	if (block->prev) {
		fpage_free_block_t* space_prev_block = (fpage_free_block_t*)((uintptr_t)block->prev - __builtin_offsetof(fpage_free_block_t, next));
		fpage_free_block_t* prev_block = space_map_phys_fixed_offset_type(space, space_prev_block);
		uint64_t prev_byte_size = prev_block->page_count * FPAGE_PAGE_SIZE;
		fpage_free_block_t* phys_prev_block_end = (fpage_free_block_t*)((uintptr_t)space_prev_block + prev_byte_size);

		if (phys_prev_block_end == space_block) {
			space_remove_virtual_free_block(space, space_block);
			prev_block->page_count += curr_page_count;
			return space_prev_block;
		}
	}

	return NULL;
};

/**
 * Allocates a virtual region of the given size in the given address space.
 *
 * @pre The region head lock and all the region locks MUST NOT be held.
 *      Additionally, the L4 table lock MUST be held.
 */
void* fpage_space_allocate_virtual(fpage_space_t* space, size_t page_count, uint8_t alignment_power, size_t* out_allocated_page_count, bool user) {
	if (alignment_power < FPAGE_MIN_ALIGNMENT) {
		alignment_power = FPAGE_MIN_ALIGNMENT;
	}

	uintptr_t alignment_mask = ((uintptr_t)1 << alignment_power) - 1;

	fpage_free_block_t* space_candidate_block = NULL;
	uint64_t space_candidate_pages = 0;

	// look for the first usable block
	for (fpage_free_block_t* space_block = space->blocks; space_block != NULL; space_block = space_map_phys_fixed_offset_type(space, space_block)->next) {
		fpage_free_block_t* block = space_map_phys_fixed_offset_type(space, space_block);

		if (block->page_count < page_count) {
			continue;
		}

		if (((uintptr_t)space_block & alignment_mask) != 0) {
			if (block->page_count > 1) {
				// the start of this block isn't aligned the way we want;
				// let's see if a subblock within it is...
				uintptr_t next_aligned_address = ((uintptr_t)space_block & ~alignment_mask) + (alignment_mask + 1);
				uint64_t byte_size = block->page_count * FPAGE_PAGE_SIZE;
				uintptr_t block_end = (uintptr_t)space_block + byte_size;

				if (next_aligned_address > (uintptr_t)space_block && next_aligned_address < block_end) {
					// okay great, the next aligned address falls within this block.
					// however, let's see if the subblock is big enough for us.
					uint64_t remaining_pages = (block_end - next_aligned_address) / FPAGE_PAGE_SIZE;

					if (remaining_pages < page_count) {
						// this subblock isn't big enough
						continue;
					}

					// great, we have an aligned subblock big enough; let's go ahead and save this candidate
				} else {
					// nope, the next aligned address isn't in this block.
					continue;
				}
			} else {
				// can't split up a 1-page block to get an aligned block big enough
				continue;
			}
		}

		space_candidate_block = space_block;
		space_candidate_pages = block->page_count;
		break;
	}

	// uh-oh, we don't have any free blocks big enough
	if (!space_candidate_block) {
		return NULL;
	}

	// okay, we've chosen our candidate region. un-free it
	space_remove_virtual_free_block(space, space_candidate_block);

	if (((uintptr_t)space_candidate_block & alignment_mask) != 0) {
		// alright, if we have an unaligned candidate block, we've already determined that
		// it does have an aligned subblock big enough for us, so let's split up the block to get it.

		uintptr_t next_aligned_address = ((uintptr_t)space_candidate_block & ~alignment_mask) + (alignment_mask + 1);
		uint64_t pages_before = (next_aligned_address - (uintptr_t)space_candidate_block) / FPAGE_PAGE_SIZE;

		fassert(pages_before > 0);
		space_insert_virtual_free_block(space, space_candidate_block, pages_before);

		space_candidate_block = (fpage_free_block_t*)next_aligned_address;
		space_candidate_pages -= pages_before;

		// the candidate block is now the aligned candidate block.
		// however, the aligned candidate block may have been too big for us,
		// so let's continue on with the usual shrinking/splitting case.
	}

	// we might have gotten a bigger block than we wanted. split it up.
	if (space_candidate_pages > page_count) {
		uint64_t byte_size = page_count * FPAGE_PAGE_SIZE;
		uintptr_t candidate_block_end = (uintptr_t)space_candidate_block + byte_size;
		space_insert_virtual_free_block(space, (fpage_free_block_t*)candidate_block_end, space_candidate_pages - page_count);
	}

	// alright, we now have the right-size block.

#if FERRO_KASAN
	if (space == fpage_space_kernel()) {
		fpage_map_kasan_shadow(NULL, (uintptr_t)space_candidate_block, 0, page_count);
		ferro_kasan_clean((uintptr_t)space_candidate_block, page_count * FPAGE_PAGE_SIZE);
	}
#endif

	// ...let the user know how much we actually gave them (if they want to know that)...
	if (out_allocated_page_count) {
		*out_allocated_page_count = page_count;
	}

	// ...and finally, give them their new block
	return space_candidate_block;
};

/**
 * @pre MUST be holding the L4 table lock and MUST NOT be holding the regions head lock nor any of the region locks.
 */
bool fpage_space_free_virtual(fpage_space_t* space, void* virtual, size_t page_count, bool user) {
#if FPAGE_VMM_CHECK_FREE
	uintptr_t page_addr = (uintptr_t)virtual;
	uintptr_t page_end = page_addr + (page_count * FPAGE_PAGE_SIZE);
#endif

	fpage_free_block_t* space_block = virtual;

#if FPAGE_VMM_CHECK_FREE
	for (fpage_free_block_t* block = space->blocks; block != NULL; block = space_map_phys_fixed_offset_type(space, block)->next) {
		uintptr_t block_addr = (uintptr_t)block;
		uintptr_t block_end = block_addr + (space_map_phys_fixed_offset_type(space, block)->page_count * FPAGE_PAGE_SIZE);

		if (
			(page_addr >= block_addr && page_addr < block_end) ||
			(page_end > block_addr && page_end <= block_end)
		) {
			fpanic("Trying to free page that's not in-use");
		}
	}
#endif

	space_insert_virtual_free_block(space, space_block, page_count);

#if FERRO_KASAN
	if (space == fpage_space_kernel()) {
		ferro_kasan_poison((uintptr_t)space_block, page_count);
	}
#endif

	while (space_block) {
		space_block = space_merge_free_blocks(space, space_block);
	}

	return true;
};

// must be holding the L4 table lock
FERRO_NO_KASAN
void fpage_space_flush_mapping_internal(fpage_space_t* space, void* address, size_t page_count, bool needs_flush, bool also_break, bool also_free) {
	while (page_count > 0) {
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
			page_count -= (page_count < FPAGE_SUPER_LARGE_PAGE_COUNT) ? page_count : FPAGE_SUPER_LARGE_PAGE_COUNT;
			address = (void*)((uintptr_t)address + FPAGE_SUPER_LARGE_PAGE_SIZE);
			continue;
		}

		// at L4, large pages are not allowed, so no need to check

		table = map_phys_fixed_offset((void*)fpage_entry_address(entry));
		entry = table->entries[l3];

		// check if L3 is active
		if (!fpage_entry_is_active(entry)) {
			// we have to mark pages that were previously bound-on-demand as normal inactive pages
			if (also_break && fpage_entry_address(entry) == ON_DEMAND_MAGIC) {
				table->entries[l3] = fpage_entry_mark_active(fpage_very_large_page_entry(0, false), false);
			}

			page_count -= (page_count < FPAGE_VERY_LARGE_PAGE_COUNT) ? page_count : FPAGE_VERY_LARGE_PAGE_COUNT;
			address = (void*)((uintptr_t)address + FPAGE_VERY_LARGE_PAGE_SIZE);
			continue;
		}

		// at L3, there might be a very large page instead of a table
		if (fpage_entry_is_large_page_entry(entry)) {
			// okay, so this is a very large page; we MUST have >= 512*512 pages
			if (page_count < FPAGE_VERY_LARGE_PAGE_COUNT) {
				// okay, we don't want this
				// while it is possible to flush the very large page and be done with it,
				// it doesn't make sense for any of the code calling this function to have this case
				fpanic("Found very large page, but flushing only part");
			}

			if (also_break) {
				table->entries[l3] = fpage_entry_mark_active(entry, false);
			}

			// okay, flush the very large page and continue
			if (needs_flush) {
				uintptr_t start_flush = fpage_make_virtual_address(l4, l3, 0, 0, 0);
				uintptr_t end_flush = start_flush + FPAGE_VERY_LARGE_PAGE_SIZE;
				fpage_invalidate_tlb_for_range((void*)start_flush, (void*)end_flush);
			}

			if (also_free) {
				fpage_pmm_free_frame((void*)fpage_entry_address(entry), FPAGE_VERY_LARGE_PAGE_COUNT);
			}

			page_count -= FPAGE_VERY_LARGE_PAGE_COUNT;
			address = (void*)((uintptr_t)address + FPAGE_VERY_LARGE_PAGE_SIZE);
			continue;
		}

		table = map_phys_fixed_offset((void*)fpage_entry_address(entry));
		entry = table->entries[l2];

		// check if L2 is active
		if (!fpage_entry_is_active(entry)) {
			if (also_break && fpage_entry_address(entry) == ON_DEMAND_MAGIC) {
				table->entries[l2] = fpage_entry_mark_active(fpage_large_page_entry(0, false), false);
			}

			page_count -= (page_count < FPAGE_LARGE_PAGE_COUNT) ? page_count : FPAGE_LARGE_PAGE_COUNT;
			address = (void*)((uintptr_t)address + FPAGE_LARGE_PAGE_SIZE);
			continue;
		}

		// at L2, there might be a large page instead of a table
		if (fpage_entry_is_large_page_entry(entry)) {
			// like before, this is a large page; we MUST have >= 512 pages
			if (page_count < FPAGE_LARGE_PAGE_COUNT) {
				// again, we don't want this
				fpanic("Found large page, but flushing only part");
			}

			if (also_break) {
				table->entries[l2] = fpage_entry_mark_active(entry, false);
			}

			// okay, flush the large page and continue
			if (needs_flush) {
				uintptr_t start_flush = fpage_make_virtual_address(l4, l3, l2, 0, 0);
				uintptr_t end_flush = start_flush + FPAGE_LARGE_PAGE_SIZE;
				fpage_invalidate_tlb_for_range((void*)start_flush, (void*)end_flush);
			}

			if (also_free) {
				fpage_pmm_free_frame((void*)fpage_entry_address(entry), FPAGE_LARGE_PAGE_COUNT);
			}

			page_count -= FPAGE_LARGE_PAGE_COUNT;
			address = (void*)((uintptr_t)address + FPAGE_LARGE_PAGE_SIZE);
			continue;
		}

		table = map_phys_fixed_offset((void*)fpage_entry_address(entry));
		entry = table->entries[l1];

		// check if L1 is active
		if (!fpage_entry_is_active(entry)) {
			if (also_break && fpage_entry_address(entry) == ON_DEMAND_MAGIC) {
				table->entries[l1] = fpage_entry_mark_active(fpage_page_entry(0, false), false);
			}

			--page_count;
			address = (void*)((uintptr_t)address + FPAGE_PAGE_SIZE);
			continue;
		}

		if (also_break) {
			table->entries[l1] = fpage_entry_mark_active(entry, false);
		}

		// at L1, there can only be a single page
		if (needs_flush) {
			uintptr_t start_flush = fpage_make_virtual_address(l4, l3, l2, l1, 0);
			uintptr_t end_flush = start_flush + FPAGE_PAGE_SIZE;
			fpage_invalidate_tlb_for_range((void*)start_flush, (void*)end_flush);
		}

		if (also_free) {
			fpage_pmm_free_frame((void*)fpage_entry_address(entry), 1);
		}

		--page_count;
		address = (void*)((uintptr_t)address + FPAGE_PAGE_SIZE);
	}

	if (needs_flush) {
		// FIXME: figure out why the precise flush doesn't work
		fpage_invalidate_tlb_for_active_space();
	}
};

/**
 * @note The table used with the first call to this function is not freed by it, no matter if `also_free` is used
 *       Also, `fpage_flush_table_internal` is a terrible name for this, because if @p needs_flush is `false`,
 *       nothing will actually be flushed from the TLB
 *
 * @pre If flushing a table within an address space, MUST be holding the L4 table lock.
 */
static void fpage_flush_table_internal(fpage_table_t* phys_table, size_t level_count, uint16_t l4, uint16_t l3, uint16_t l2, bool needs_flush, bool flush_recursive_too, bool also_break, bool also_free) {
	fpage_table_t* virt_table;

	for (size_t i = 0; i < TABLE_ENTRY_COUNT; ++i) {
		virt_table = map_phys_fixed_offset(phys_table);
		uint64_t entry = virt_table->entries[i];
		size_t page_count = 1;

		if (!fpage_entry_is_active(entry)) {
			if (also_break && fpage_entry_address(entry)) {
				switch (level_count) {
					case 1:
						virt_table->entries[i] = fpage_entry_mark_active(fpage_very_large_page_entry(0, false), false);
						break;
					case 2:
						virt_table->entries[i] = fpage_entry_mark_active(fpage_large_page_entry(0, false), false);
						break;
					case 3:
						virt_table->entries[i] = fpage_entry_mark_active(fpage_page_entry(0, false), false);
						break;
				}
			}

			continue;
		}

		if (also_break) {
			virt_table->entries[i] = fpage_entry_mark_active(entry, false);
		}

		switch (level_count) {
			case 0: {
				// the table is an L4 table
				// the entry is an L3 table
				fpage_flush_table_internal((void*)fpage_entry_address(entry), 1, i, 0, 0, needs_flush, flush_recursive_too, also_break, also_free);
			} break;

			case 1: {
				// the table is an L3 table
				// the entry is either an L2 table or a 1GiB very large page
				if (fpage_entry_is_large_page_entry(entry)) {
					// the entry is a 1GiB very large page
					if (needs_flush) {
						uintptr_t start_flush = fpage_make_virtual_address(l4, i, 0, 0, 0);
						uintptr_t end_flush = start_flush + FPAGE_VERY_LARGE_PAGE_SIZE;
						fpage_invalidate_tlb_for_range((void*)start_flush, (void*)end_flush);
					}
					page_count = FPAGE_VERY_LARGE_PAGE_COUNT;
				} else {
					// the entry is an L2 table
					fpage_flush_table_internal((void*)fpage_entry_address(entry), 2, l4, i, 0, needs_flush, flush_recursive_too, also_break, also_free);
				}
			} break;

			case 2: {
				// the table is an L2 table
				// the entry is either an L1 table or a 2MiB large page
				if (fpage_entry_is_large_page_entry(entry)) {
					// the entry is a 2MiB large page
					if (needs_flush) {
						uintptr_t start_flush = fpage_make_virtual_address(l4, l3, i, 0, 0);
						uintptr_t end_flush = start_flush + FPAGE_LARGE_PAGE_SIZE;
						fpage_invalidate_tlb_for_range((void*)start_flush, (void*)end_flush);
					}
					page_count = FPAGE_LARGE_PAGE_COUNT;
				} else {
					// the entry is an L1 table
					fpage_flush_table_internal((void*)fpage_entry_address(entry), 3, l4, l3, i, needs_flush, flush_recursive_too, also_break, also_free);
				}
			} break;

			case 3: {
				// the table is an L1 table
				// the entry is a page entry

				if (needs_flush) {
					uintptr_t start_flush = fpage_make_virtual_address(l4, l3, l2, i, 0);
					uintptr_t end_flush = start_flush + FPAGE_PAGE_SIZE;
					fpage_invalidate_tlb_for_range((void*)start_flush, (void*)end_flush);
				}
			} break;
		}

		if (also_free) {
			fpage_pmm_free_frame((void*)fpage_entry_address(entry), page_count);
		}
	}

	if (flush_recursive_too) {
		//uintptr_t start_flush = fpage_table_recursive_address(level_count, l4, l3, l2);
		//uintptr_t end_flush = start_flush + FPAGE_PAGE_SIZE;
		//fpage_invalidate_tlb_for_range((void*)start_flush, (void*)end_flush);
		fpage_invalidate_tlb_for_active_space();
	}

	if (needs_flush) {
		// FIXME: the precise flush doesn't seem to work properly
		fpage_invalidate_tlb_for_active_space();
	}
};

FERRO_WUR ferr_t fpage_space_init(fpage_space_t* space) {
	space->l4_table = fpage_pmm_allocate_frame(1, 0, NULL);

	if (!space->l4_table) {
		return ferr_temporary_outage;
	}

	fpage_table_t* table = map_phys_fixed_offset(space->l4_table);

	simple_memset(table, 0, sizeof(fpage_table_t));

	flock_spin_intsafe_init(&space->lock);

	// initialize the VMM allocator block list

	space->blocks = NULL;
	space->vmm_allocator_start = fpage_make_virtual_address(FPAGE_USER_L4_MAX, 0, 0, 0, 0);
	space->vmm_allocator_page_count = ((FPAGE_USER_MAX + 1) - space->vmm_allocator_start) / FPAGE_PAGE_SIZE;

	space_insert_virtual_free_block(space, (fpage_free_block_t*)space->vmm_allocator_start, space->vmm_allocator_page_count);

	space->mappings = NULL;

	fwaitq_init(&space->space_destruction_waiters);

	return ferr_ok;
};

void fpage_space_destroy(fpage_space_t* space) {
	fpage_space_mapping_t* next = NULL;

	fwaitq_wake_many(&space->space_destruction_waiters, SIZE_MAX);

	flock_spin_intsafe_lock(&space->lock);

	for (fpage_space_mapping_t* curr = space->mappings; curr != NULL; curr = next) {
		next = curr->next;

		if (curr->mapping) {
			// this will ensure that any pages we may have mapped in for the mapping will be marked as inactive,
			// which allows us to use fpage_flush_table_internal() with `also_free == true` and avoid (incorrectly)
			// freeing frames allocated for mappings (those are freed by the mapping object itself)
			fpage_space_flush_mapping_internal(space, (void*)curr->virtual_address, curr->page_count, false, true, false);

			fpage_mapping_release(curr->mapping);
		} else {
			// other mapping entries aren't actually backed by a shareable mapping, so we can go ahead and free them normally
		}

		// no need to unlink it since the space is being destroyed

		FERRO_WUR_IGNORE(fslab_free(&fpage_space_mapping_slab, curr));
	}

	fpage_flush_table_internal(space->l4_table, 0, 0, 0, 0, fpage_space_active(space), fpage_space_active(space), true, true);

	// the VMM allocator's block list is placed within the address space,
	// so the above call should've already taken care of freeing all of its blocks.

	space->blocks = NULL;
	space->vmm_allocator_start = 0;
	space->vmm_allocator_page_count = 0;

	fpage_pmm_free_frame(space->l4_table, 1);
	space->l4_table = NULL;

	// FIXME: we need to check all the CPU cores and see if any one of them is using this address space
	// XXX: actually, scratch that. the only time we should be destroying an address space is once we're
	//      certain that no one is using it, so this shouldn't be an issue.
	fpage_space_t** current_address_space = fpage_space_current_pointer();
	if (*current_address_space == space) {
		*current_address_space = &fpage_vmm_kernel_address_space;
	}

	flock_spin_intsafe_unlock(&space->lock);
};

FERRO_NO_KASAN
void fpage_vmm_init(void) {
	uintptr_t virt_start = FERRO_KERNEL_VIRTUAL_START;

	// we need to enumerate and set up available virtual memory regions
	//
	// for now, we only need to set up the kernel address space

	// once we reach the maximum, it'll wrap around to 0
	while (virt_start != 0) {
		size_t virt_page_count = 0;
		size_t l4_index = FPAGE_VIRT_L4(virt_start);
		size_t l3_index = FPAGE_VIRT_L3(virt_start);
		size_t l2_index = FPAGE_VIRT_L2(virt_start);
		size_t l1_index = FPAGE_VIRT_L1(virt_start);
		uint64_t entry = 0;

		// find the first free address

		for (; l4_index < TABLE_ENTRY_COUNT; ++l4_index) {
			// don't touch the recursive entry or the offset index
			if (l4_index == fpage_root_recursive_index || l4_index == fpage_root_offset_index) {
				continue;
			}

			entry = fpage_table_load(1, l4_index, 0, 0, 0);

			// if the l4 entry is inactive, it's free! otherwise, we need to check further.
			if (!fpage_entry_is_active(entry)) {
				l3_index = 0;
				l2_index = 0;
				l1_index = 0;
				goto determine_size;
			}

			for (; l3_index < TABLE_ENTRY_COUNT; ++l3_index) {
				entry = fpage_table_load(2, l4_index, l3_index, 0, 0);

				// ditto
				if (!fpage_entry_is_active(entry)) {
					l2_index = 0;
					l1_index = 0;
					goto determine_size;
				}

				// we know that any address covered by the large page entry is not free, so try again on the next index
				if (fpage_entry_is_large_page_entry(entry)) {
					continue;
				}

				for (; l2_index < TABLE_ENTRY_COUNT; ++l2_index) {
					entry = fpage_table_load(3, l4_index, l3_index, l2_index, 0);

					if (!fpage_entry_is_active(entry)) {
						goto determine_size;
					}

					// ditto
					if (fpage_entry_is_large_page_entry(entry)) {
						l1_index = 0;
						continue;
					}

					for (; l1_index < TABLE_ENTRY_COUNT; ++l1_index) {
						entry = fpage_table_load(4, l4_index, l3_index, l2_index, l1_index);

						if (!fpage_entry_is_active(entry)) {
							goto determine_size;
						}
					}
				}

				l2_index = 0;
			}

			l3_index = 0;
		}

		// if we got here, there were no free addresses
		virt_start = 0;
		break;

	determine_size:
		virt_start = fpage_make_virtual_address(l4_index, l3_index, l2_index, l1_index, 0);

		for (; l4_index < TABLE_ENTRY_COUNT; ++l4_index) {
			entry = fpage_table_load(1, l4_index, 0, 0, 0);

			// not active? great, we've got an entire 512GiB region free!
			if (!fpage_entry_is_active(entry)) {
				virt_page_count += TABLE_ENTRY_COUNT * TABLE_ENTRY_COUNT * TABLE_ENTRY_COUNT;
				l3_index = 0;
				l2_index = 0;
				l1_index = 0;
				continue;
			}

			for (; l3_index < TABLE_ENTRY_COUNT; ++l3_index) {
				entry = fpage_table_load(2, l4_index, l3_index, 0, 0);

				// again: not active? awesome, we've got an entire 1GiB region free!
				if (!fpage_entry_is_active(entry)) {
					virt_page_count += TABLE_ENTRY_COUNT * TABLE_ENTRY_COUNT;
					l2_index = 0;
					l1_index = 0;
					continue;
				}

				// we know that any address covered by the large page entry is not free, so we're done
				if (fpage_entry_is_large_page_entry(entry)) {
					goto done_determining_size;
				}

				for (; l2_index < TABLE_ENTRY_COUNT; ++l2_index) {
					entry = fpage_table_load(3, l4_index, l3_index, l2_index, 0);

					// once again: not active? neat, we've got a 2MiB region free!
					if (!fpage_entry_is_active(entry)) {
						l1_index = 0;
						virt_page_count += TABLE_ENTRY_COUNT;
						continue;
					}

					// ditto: large page == non-free; we're done
					if (fpage_entry_is_large_page_entry(entry)) {
						goto done_determining_size;
					}

					for (; l1_index < TABLE_ENTRY_COUNT; ++l1_index) {
						entry = fpage_table_load(4, l4_index, l3_index, l2_index, l1_index);

						// not active? cool, we've got a free page.
						if (!fpage_entry_is_active(entry)) {
							++virt_page_count;
							continue;
						}

						// it's active, so we've found a non-free page
						goto done_determining_size;
					}

					l1_index = 0;
				}

				l2_index = 0;
			}

			l3_index = 0;
		}

	done_determining_size:
		// 0 == NULL
		// since it's a special address, we don't want to use it at all.
		// skip the first page if this is the case.
		if (virt_start == 0) {
			--virt_page_count;
			virt_start += FPAGE_PAGE_SIZE;
		}

		if (virt_page_count == 0) {
			continue;
		}

		space_insert_virtual_free_block(&fpage_vmm_kernel_address_space, (fpage_free_block_t*)virt_start, virt_page_count);

		// this will overflow to 0 for the last region in the virtual address space
		virt_start += virt_page_count * FPAGE_PAGE_SIZE;
	}
};
