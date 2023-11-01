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
 * Physical memory allocation.
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
#include <ferro/kasan.h>

// how many pages to prefault when doing a prefault for physical memory allocation
//
// this should remain smaller than the prefault page count for general paging
#define PREFAULT_PAGE_COUNT_PHYS 1

#ifndef FPAGE_PMM_CHECK_FREE
	#define FPAGE_PMM_CHECK_FREE 0
#endif

#ifndef FPAGE_PMM_CLEAR_ON_REMOVE
	#define FPAGE_PMM_CLEAR_ON_REMOVE 0
#endif

#define FPAGE_PMM_FIRST_USABLE 0x10000

atomic_size_t fpage_pmm_frames_in_use = 0;
uint64_t fpage_pmm_total_page_count = 0;

static fpage_free_block_t* blocks = NULL;
static flock_spin_intsafe_t blocks_lock = FLOCK_SPIN_INTSAFE_INIT;

/**
 * Inserts the given block into the appropriate place in the block list.
 *
 * The blocks lock MUST be held.
 */
FERRO_NO_KASAN
static void insert_free_block(fpage_free_block_t* phys_block, size_t block_page_count) {
	fpage_free_block_t** block_prev = NULL;
	fpage_free_block_t* block_next = blocks;
	fpage_free_block_t* block = NULL;

	while (block_next && block_next < phys_block) {
		block_prev = &block_next->next;
		block_next = *map_phys_fixed_offset_type(block_prev);
	}

	block = map_phys_fixed_offset_type(phys_block);
	block->prev = block_prev;
	block->next = block_next;
	block->page_count = block_page_count;

	if (block_prev) {
		*map_phys_fixed_offset_type(block_prev) = phys_block;
	} else {
		blocks = phys_block;
	}

	if (block_next) {
		map_phys_fixed_offset_type(block_next)->prev = &phys_block->next;
	}

	fpage_pmm_frames_in_use -= block_page_count;
};

/**
 * Removes the given block from the block list.
 *
 * The blocks lock MUST be held.
 */
FERRO_NO_KASAN
static void remove_free_block(fpage_free_block_t* phys_block) {
	fpage_free_block_t* block = map_phys_fixed_offset_type(phys_block);

	if (block->prev) {
		*map_phys_fixed_offset_type(block->prev) = block->next;
	} else {
		blocks = block->next;
	}

	if (block->next) {
		map_phys_fixed_offset_type(block->next)->prev = block->prev;
	}

#if FPAGE_PMM_CLEAR_ON_REMOVE
	block->prev = NULL;
	block->next = NULL;
	block->page_count = 0;
#endif

	fpage_pmm_frames_in_use += block->page_count;
};

FERRO_NO_KASAN
static fpage_free_block_t* merge_free_blocks(fpage_free_block_t* phys_block) {
	fpage_free_block_t* block = map_phys_fixed_offset_type(phys_block);
	uint64_t curr_page_count = block->page_count;
	uint64_t byte_size = curr_page_count * FPAGE_PAGE_SIZE;
	fpage_free_block_t* phys_block_end = (fpage_free_block_t*)((uintptr_t)phys_block + byte_size);

	if (block->next) {
		if (block->next == phys_block_end) {
			uint64_t next_page_count = map_phys_fixed_offset_type(block->next)->page_count;
			remove_free_block(block->next);
			block->page_count += next_page_count;
			fpage_pmm_frames_in_use -= next_page_count;
			return phys_block;
		}
	}

	if (block->prev) {
		fpage_free_block_t* phys_prev_block = (fpage_free_block_t*)((uintptr_t)block->prev - __builtin_offsetof(fpage_free_block_t, next));
		fpage_free_block_t* prev_block = map_phys_fixed_offset_type(phys_prev_block);
		uint64_t prev_byte_size = prev_block->page_count * FPAGE_PAGE_SIZE;
		fpage_free_block_t* phys_prev_block_end = (fpage_free_block_t*)((uintptr_t)phys_prev_block + prev_byte_size);

		if (phys_prev_block_end == phys_block) {
			remove_free_block(phys_block);
			prev_block->page_count += curr_page_count;
			fpage_pmm_frames_in_use -= curr_page_count;
			return phys_prev_block;
		}
	}

	return NULL;
};

/**
 * Allocates a physical frame of the given size.
 *
 * The ::regions_head lock and all the region locks MUST NOT be held.
 */
FERRO_NO_KASAN
void* fpage_pmm_allocate_frame(size_t page_count, uint8_t alignment_power, size_t* out_allocated_page_count) {
	// prefault now, before we acquire any locks
	fpage_prefault_stack(PREFAULT_PAGE_COUNT_PHYS);
	flock_spin_intsafe_lock(&blocks_lock);

	if (alignment_power < FPAGE_MIN_ALIGNMENT) {
		alignment_power = FPAGE_MIN_ALIGNMENT;
	}

	uintptr_t alignment_mask = ((uintptr_t)1 << alignment_power) - 1;

	fpage_free_block_t* candidate_block = NULL;
	uint64_t candidate_pages = 0;

	// look for the first usable block
	for (fpage_free_block_t* phys_block = blocks; blocks != NULL; phys_block = map_phys_fixed_offset_type(phys_block)->next) {
		fpage_free_block_t* block = map_phys_fixed_offset_type(phys_block);

		if (block->page_count < page_count) {
			continue;
		}

		if (((uintptr_t)phys_block & alignment_mask) != 0) {
			if (block->page_count > 1) {
				// the start of this block isn't aligned the way we want;
				// let's see if a subblock within it is...
				uintptr_t next_aligned_address = ((uintptr_t)phys_block & ~alignment_mask) + (alignment_mask + 1);
				uint64_t byte_size = block->page_count * FPAGE_PAGE_SIZE;
				uintptr_t block_end = (uintptr_t)phys_block + byte_size;

				if (next_aligned_address > (uintptr_t)phys_block && next_aligned_address < block_end) {
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

		candidate_block = phys_block;
		candidate_pages = block->page_count;
		break;
	}

	// uh-oh, we don't have any free blocks big enough
	if (!candidate_block) {
		flock_spin_intsafe_unlock(&blocks_lock);
		return NULL;
	}

	// the blocks lock is held here

	// okay, we've chosen our candidate region. un-free it
	remove_free_block(candidate_block);

	if (((uintptr_t)candidate_block & alignment_mask) != 0) {
		// alright, if we have an unaligned candidate block, we've already determined that
		// it does have an aligned subblock big enough for us, so let's split up the block to get it.

		uintptr_t next_aligned_address = ((uintptr_t)candidate_block & ~alignment_mask) + (alignment_mask + 1);
		uint64_t pages_before = (next_aligned_address - (uintptr_t)candidate_block) / FPAGE_PAGE_SIZE;

		fassert(pages_before > 0);
		insert_free_block(candidate_block, pages_before);

		candidate_block = (fpage_free_block_t*)next_aligned_address;
		candidate_pages -= pages_before;

		// the candidate block is now the aligned candidate block.
		// however, the aligned candidate block may have been too big for us,
		// so let's continue on with the usual shrinking/splitting case.
	}

	// we might have gotten a bigger block than we wanted. split it up.
	if (candidate_pages > page_count) {
		uint64_t byte_size = page_count * FPAGE_PAGE_SIZE;
		uintptr_t candidate_block_end = (uintptr_t)candidate_block + byte_size;
		insert_free_block((fpage_free_block_t*)candidate_block_end, candidate_pages - page_count);
	}

	// alright, we now have the right-size block.

	// we can now release the regions lock
	flock_spin_intsafe_unlock(&blocks_lock);

#if FERRO_KASAN
	if (out_allocated_page_count != &fpage_map_kasan_pmm_allocate_marker) {
		// clear the KASan shadow for the offset-mapped memory
		fpage_map_kasan_shadow(NULL, (uintptr_t)map_phys_fixed_offset(candidate_block), (uintptr_t)candidate_block, page_count);
		ferro_kasan_clean((uintptr_t)map_phys_fixed_offset(candidate_block), page_count * FPAGE_PAGE_SIZE);
	}
#endif

	// ...let the user know how much we actually gave them (if they want to know that)...
	if (out_allocated_page_count) {
		*out_allocated_page_count = page_count;
	}

#if FPAGE_DEBUG_LOG_FRAMES
	if (fpage_logging_available) {
		fconsole_logf("Allocating frame %p (page count = %zu)\n", candidate_block, page_count);
	}
#endif

	// ...and finally, give them their new block
	return candidate_block;
};

/**
 * Frees a physical frame of the given size.
 *
 * The ::regions_head lock and all the region locks MUST NOT be held.
 */
void fpage_pmm_free_frame(void* frame, size_t page_count) {
#if FPAGE_PMM_CHECK_FREE
	uintptr_t frame_addr = (uintptr_t)frame;
	uintptr_t frame_end = frame_addr + (page_count * FPAGE_PAGE_SIZE);
#endif

	// prefault now, before we acquire any locks
	fpage_prefault_stack(PREFAULT_PAGE_COUNT_PHYS);
	flock_spin_intsafe_lock(&blocks_lock);

#if FPAGE_DEBUG_LOG_FRAMES
	if (fpage_logging_available) {
		fconsole_logf("Freeing frame %p (page count = %zu)\n", frame, page_count);
	}
#endif

#if FPAGE_PMM_CHECK_FREE
	for (fpage_free_block_t* block = blocks; block != NULL; block = ferro_kasan_load_unchecked_auto(&map_phys_fixed_offset_type(block)->next)) {
		uintptr_t block_addr = (uintptr_t)block;
		uintptr_t block_end = block_addr + (ferro_kasan_load_unchecked_auto(&map_phys_fixed_offset_type(block)->page_count) * FPAGE_PAGE_SIZE);

		if (
			(frame_addr >= block_addr && frame_addr < block_end) ||
			(frame_end > block_addr && frame_end <= block_end)
		) {
			fpanic("Trying to free frame that's not in-use");
		}
	}
#endif

	insert_free_block(frame, page_count);

#if FERRO_KASAN
	// poison the KASan shadow for the offset-mapped memory
	ferro_kasan_poison((uintptr_t)map_phys_fixed_offset(frame), page_count);
#endif

	while (frame) {
		frame = merge_free_blocks(frame);
	}

	// we can now drop the lock
	flock_spin_intsafe_unlock(&blocks_lock);
};

FERRO_NO_KASAN
void fpage_pmm_init(ferro_memory_region_t* memory_regions, size_t memory_region_count) {
	// okay, now we need to initialize each physical region

	for (size_t i = 0; i < memory_region_count; ++i) {
		const ferro_memory_region_t* region = &memory_regions[i];
		size_t pages_allocated = 0;
		size_t page_count = region->page_count;
		uintptr_t physical_start = region->physical_start;
		size_t bitmap_byte_count = 0;
		void* usable_start = NULL;
		size_t extra_bitmap_page_count = 0;

		// skip non-general memory
		if (region->type != ferro_memory_region_type_general) {
			continue;
		}

		// we reserve low memory for special uses (e.g. SMP initialization on x86_64)
		while (page_count > 0 && physical_start < FPAGE_PMM_FIRST_USABLE) {
			--page_count;
			physical_start += FPAGE_PAGE_SIZE;
		}

		if (page_count == 0) {
			continue;
		}

		// okay, we're definitely going to use this region
		insert_free_block((void*)physical_start, page_count);
		fpage_pmm_total_page_count += page_count;
	}

	// initialize the frames-in-use counter to 0
	fpage_pmm_frames_in_use = 0;
};

#ifndef FERRO_HOST_TESTING
void fpage_log_early(void) {
	// we're early, so we're running in a uniprocessor environment;
	// all we have to do is disable interrupts and we don't need to take any locks
	fint_disable();

	for (fpage_free_block_t* phys_block = blocks; phys_block != NULL; phys_block = map_phys_fixed_offset_type(phys_block)->next) {
		fconsole_logf("PMM: physical region %p-%p\n", phys_block, (char*)phys_block + (map_phys_fixed_offset_type(phys_block)->page_count * FPAGE_PAGE_SIZE));
	}

	fint_enable();
};
#endif
