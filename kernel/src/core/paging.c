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
 * Physical and virtual memory allocation.
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

// magic value used to identify pages that need to mapped on-demand
#define ON_DEMAND_MAGIC (0xdeadfeeedULL << FPAGE_VIRT_L1_SHIFT)

// how many pages to prefault when doing a prefault
#define PREFAULT_PAGE_COUNT 2

#if FERRO_ARCH == FERRO_ARCH_x86_64
	#define USE_TEMPORARY_MAPPING 0
#elif FERRO_ARCH == FERRO_ARCH_aarch64
	#define USE_TEMPORARY_MAPPING 0
#else
	#error Unrecognized/unsupported CPU architecture! (see src/core/paging.c)
#endif

#define TABLE_ENTRY_COUNT (sizeof(root_table->entries) / sizeof(*root_table->entries))
// coefficient that is multiplied by the amount of physical memory available to determine the maximum amount of virtual memory
// the buddy allocator can use. more virtual memory than this can be used, it's just that it'll use a less efficient method of allocation.
#define MAX_VIRTUAL_KERNEL_BUDDY_ALLOCATOR_PAGE_COUNT_COEFFICIENT 16

// for both physical and virtual memory allocation, this file uses an algorithm inspired by the buddy allocator algorithm
//
// it varies on a few key points:
// * there is no limit on the number of nodes each bucket can have
// * common implementations of the buddy allocator use a bitmap to keep track of which nodes are free and which aren't.
//   our implementation does this as well, but due to the additional freedom of not being restricted to a maximum number of nodes per bucket,
//   it requires more memory. namely, common implementations need there to be as many bits as half the maximum number of blocks.
//   our implementation requires one bit per page, not block.
//
// the bitmap has an average overhead of approximately 0.003% of the total size of a region. not too shabby.

#define HEADER_BITMAP_SPACE (FPAGE_PAGE_SIZE - sizeof(fpage_region_header_t))

// altogether, we've reserved 2 L4 indicies, which means that the maximum amount of memory
// we can use is 256TiB - (2 * 512GiB) = 255TiB.
// yeah, i think we're okay for now.

static fpage_table_t* root_table = NULL;
// the L4 index for the kernel's address space
static uint16_t kernel_l4_index = 0;
// the L3 index for the kernel's initial memory region
static uint16_t kernel_l3_index = 0;
static uint16_t root_recursive_index = TABLE_ENTRY_COUNT - 1;
static fpage_region_header_t* regions_head = NULL;

static atomic_size_t frames_in_use = 0;

// we're never going to get more physical memory, so the regions head is never going to be modified; thus, we don't need a lock
//static flock_spin_intsafe_t regions_head_lock = FLOCK_SPIN_INTSAFE_INIT;

#if USE_TEMPORARY_MAPPING
// the L2 index pointing to the temporary table
static uint16_t temporary_table_index = 0;
static fpage_table_t temporary_table FERRO_PAGE_ALIGNED = {0};
#else
/**
 * Used to map 512GiB of memory at a fixed offset.
 */
FERRO_PAGE_ALIGNED
static fpage_table_t offset_table = {0};
static uint16_t root_offset_index = TABLE_ENTRY_COUNT - 2;
#endif

static fpage_table_t kernel_address_space_root_table = {0};
static fpage_space_t kernel_address_space = {
	.l4_table = &kernel_address_space_root_table,
	.regions_head_lock = FLOCK_SPIN_INTSAFE_INIT,
	.regions_head = NULL,
	.active = true,
	.allocation_lock = FLOCK_SPIN_INTSAFE_INIT,
	.space_destruction_waiters = FWAITQ_INIT,
	.mappings_lock = FLOCK_SPIN_INTSAFE_INIT,
	.mappings = NULL,
};

#ifndef FPAGE_SPACE_CHECK_REGIONS
	#define FPAGE_SPACE_CHECK_REGIONS 0
#endif

#ifndef FPAGE_DEBUG_ALWAYS_PREBIND
	#define FPAGE_DEBUG_ALWAYS_PREBIND 0
#endif

#ifndef FPAGE_DEBUG_LOG_FAULTS
	#define FPAGE_DEBUG_LOG_FAULTS 0
#endif

#ifndef FPAGE_DEBUG_LOG_FRAMES
	#define FPAGE_DEBUG_LOG_FRAMES 0
#endif

static void page_fault_handler(void* context);

static void fpage_space_flush_mapping_internal(fpage_space_t* space, void* address, size_t page_count, bool needs_flush, bool also_break, bool also_free);

bool fpage_prefaulting_enabled = false;
bool fpage_logging_available = false;

void fpage_prefault_enable(void) {
	fpage_prefaulting_enabled = true;
};

void fpage_logging_mark_available(void) {
	fpage_logging_available = true;
};

uintptr_t fpage_virtual_address_for_table(size_t levels, uint16_t l4_index, uint16_t l3_index, uint16_t l2_index) {
	if (levels == 0) {
		return fpage_make_virtual_address(root_recursive_index, root_recursive_index, root_recursive_index, root_recursive_index, 0);
	} else if (levels == 1) {
		return fpage_make_virtual_address(root_recursive_index, root_recursive_index, root_recursive_index, l4_index, 0);
	} else if (levels == 2) {
		return fpage_make_virtual_address(root_recursive_index, root_recursive_index, l4_index, l3_index, 0);
	} else if (levels == 3) {
		return fpage_make_virtual_address(root_recursive_index, l4_index, l3_index, l2_index, 0);
	} else {
		return 0;
	}
};

FERRO_ALWAYS_INLINE size_t page_count_of_order(size_t order) {
	return 1ULL << order;
};

FERRO_ALWAYS_INLINE size_t size_of_order(size_t order) {
	return page_count_of_order(order) * FPAGE_PAGE_SIZE;
};

FERRO_ALWAYS_INLINE size_t min_order_for_page_count(size_t page_count) {
	if (page_count == 0) {
		return SIZE_MAX;
	} else {
		size_t result = ferro_bits_in_use_u64(page_count) - 1;
		if (result >= FPAGE_MAX_ORDER) {
			return FPAGE_MAX_ORDER - 1;
		}
		return (page_count > page_count_of_order(result)) ? (result + 1) : result;
	}
};

FERRO_ALWAYS_INLINE size_t max_order_of_page_count(size_t page_count) {
	if (page_count == 0) {
		return SIZE_MAX;
	} else {
		size_t result = ferro_bits_in_use_u64(page_count) - 1;
		if (result >= FPAGE_MAX_ORDER) {
			return FPAGE_MAX_ORDER - 1;
		}
		return result;
	}
};

FERRO_ALWAYS_INLINE bool table_is_in_use(const fpage_table_t* table) {
	for (size_t i = 0; i < TABLE_ENTRY_COUNT; ++i) {
		if (fpage_entry_is_active(table->entries[i])) {
			return true;
		}
	}
	return false;
};

//
// physical frame allocator
//

#if USE_TEMPORARY_MAPPING
/**
 * @param slot Must be from 0-511 (inclusive).
 */
FERRO_ALWAYS_INLINE void* map_temporarily(void* physical_address, uint16_t slot) {
	void* temporary_address = NULL;
	temporary_table.entries[slot] = fpage_page_entry((uintptr_t)physical_address, true);
	fpage_synchronize_after_table_modification();
	temporary_address = (void*)fpage_make_virtual_address(FPAGE_VIRT_L4(FERRO_KERNEL_VIRTUAL_START), FPAGE_VIRT_L3(FERRO_KERNEL_VIRTUAL_START), temporary_table_index, slot, FPAGE_VIRT_OFFSET(physical_address));
	// ideally, we should mark the page as uncacheable
	// on x86_64, this is easy. on AARCH64, this is slightly less easy (but not difficult either)
	fpage_invalidate_tlb_for_address(temporary_address);
	return temporary_address;
};
#endif

#if USE_TEMPORARY_MAPPING
/**
 * @see map_temporarily
 *
 * Like map_temporarily(), but automatically chooses the next available slot.
 * This *will* automatically wrap around once it reaches the maximum,
 * but that should be no issue, as it is unlikely that you'll need to use 512 temporary pages simultaneously.
 */
FERRO_ALWAYS_INLINE void* map_temporarily_auto(void* physical_address) {
	static size_t next_slot = 0;
	void* address = map_temporarily(physical_address, next_slot++);
	if (next_slot == TABLE_ENTRY_COUNT) {
		next_slot = 0;
	}
	return address;
};
#else
/**
 * We're using fixed offset mapping for the entire physical memory, so there's no need to do temporary mapping.
 */
FERRO_ALWAYS_INLINE void* map_temporarily_auto(void* physical_address) {
	return (void*)fpage_make_virtual_address(root_offset_index, FPAGE_VIRT_L3(physical_address), FPAGE_VIRT_L2(physical_address), FPAGE_VIRT_L1(physical_address), FPAGE_VIRT_OFFSET(physical_address));
};
#endif

#define map_temporarily_auto_type(virt) ((__typeof__((virt)))map_temporarily_auto((virt)))

/**
 * Returns the bitmap bit index for the given block.
 *
 * The parent region's lock MUST be held.
 */
FERRO_ALWAYS_INLINE size_t bitmap_bit_index_for_block(const fpage_region_header_t* parent_region, const fpage_free_block_t* block) {
	uintptr_t relative_address = 0;
	parent_region = map_temporarily_auto((void*)parent_region);
	relative_address = (uintptr_t)block - (uintptr_t)parent_region->start;
	return relative_address / FPAGE_PAGE_SIZE;
};

FERRO_ALWAYS_INLINE size_t byte_index_for_bit(size_t bit_index) {
	return bit_index / 8;
};

FERRO_ALWAYS_INLINE size_t byte_bit_index_for_bit(size_t bit_index) {
	return bit_index % 8;
};

/**
 * Returns a pointer to the byte where the bitmap entry for the given block is stored, as well as the bit index of the entry in this byte.
 *
 * The parent region's lock MUST be held.
 */
static const uint8_t* bitmap_entry_for_block(const fpage_region_header_t* phys_parent_region, const fpage_free_block_t* block, size_t* out_bit_index) {
	size_t bitmap_index = bitmap_bit_index_for_block(phys_parent_region, block);
	size_t byte_index = byte_index_for_bit(bitmap_index);
	size_t byte_bit_index = byte_bit_index_for_bit(bitmap_index);
	const uint8_t* byte = NULL;

	*out_bit_index = byte_bit_index;

	return map_temporarily_auto((void*)&phys_parent_region->bitmap[byte_index]);
};

/**
 * Returns `true` if the given block is in-use.
 *
 * The parent region's lock MUST be held.
 */
static bool block_is_in_use(const fpage_region_header_t* parent_region, const fpage_free_block_t* block) {
	size_t byte_bit_index = 0;
	const uint8_t* byte = bitmap_entry_for_block(parent_region, block, &byte_bit_index);

	return ((*byte) & (1 << byte_bit_index)) != 0;
};

/**
 * Sets whether the given block is in-use.
 *
 * The parent region's lock MUST be held.
 */
static void set_block_is_in_use(fpage_region_header_t* parent_region, const fpage_free_block_t* block, bool in_use) {
	size_t byte_bit_index = 0;
	uint8_t* byte = (uint8_t*)bitmap_entry_for_block(parent_region, block, &byte_bit_index);

	if (in_use) {
		*byte |= 1 << byte_bit_index;
	} else {
		*byte &= ~(1 << byte_bit_index);
	}
};

/**
 * Inserts the given block into the appropriate bucket in the parent region.
 *
 * The parent region's lock MUST be held.
 */
static void insert_free_block(fpage_region_header_t* phys_parent_region, fpage_free_block_t* phys_block, size_t block_page_count) {
	size_t order = max_order_of_page_count(block_page_count);

	fpage_region_header_t* parent_region = map_temporarily_auto(phys_parent_region);
	fpage_free_block_t* block = map_temporarily_auto(phys_block);

	block->prev = &phys_parent_region->buckets[order];
	block->next = parent_region->buckets[order];

	if (block->next) {
		fpage_free_block_t* virt_next = map_temporarily_auto(block->next);
		virt_next->prev = &phys_block->next;
	}

	parent_region->buckets[order] = phys_block;

	set_block_is_in_use(phys_parent_region, phys_block, false);
	frames_in_use -= block_page_count;
};

/**
 * Removes the given block from the appropriate bucket in the parent region.
 *
 * The parent region's lock MUST be held.
 */
static void remove_free_block(fpage_region_header_t* phys_parent_region, fpage_free_block_t* phys_block) {
	fpage_free_block_t** prev = NULL;
	fpage_free_block_t* next = NULL;

	fpage_free_block_t* block = map_temporarily_auto(phys_block);
	prev = map_temporarily_auto(block->prev);
	if (block->next) {
		next = map_temporarily_auto(block->next);
	}

	*prev = block->next;
	if (next) {
		next->prev = block->prev;
	}

	set_block_is_in_use(phys_parent_region, phys_block, true);
};

/**
 * Finds the region's buddy.
 *
 * The parent region's lock MUST be held.
 */
static fpage_free_block_t* find_buddy(fpage_region_header_t* parent_region, fpage_free_block_t* block, size_t block_page_count) {
	uintptr_t maybe_buddy = 0;
	uintptr_t parent_start = 0;

	parent_region = map_temporarily_auto(parent_region);
	parent_start = (uintptr_t)parent_region->start;
	maybe_buddy = (((uintptr_t)block - parent_start) ^ (block_page_count * FPAGE_PAGE_SIZE)) + parent_start;

	if (maybe_buddy + (block_page_count * FPAGE_PAGE_SIZE) > parent_start + (parent_region->page_count * FPAGE_PAGE_SIZE)) {
		return NULL;
	}

	return (fpage_free_block_t*)maybe_buddy;
};

/**
 * Reads and acquires the lock for the first region at ::regions_head.
 *
 * The first region's lock MUST NOT be held.
 */
static fpage_region_header_t* acquire_first_region(void) {
	fpage_region_header_t* region;
	//flock_spin_intsafe_lock(&regions_head_lock);
	region = regions_head;
	if (region) {
		flock_spin_intsafe_lock(&((fpage_region_header_t*)map_temporarily_auto(region))->lock);
	}
	//flock_spin_intsafe_unlock(&regions_head_lock);
	return region;
};

/**
 * Reads and acquires the lock the lock for the next region after the given region.
 * Afterwards, it releases the lock for the given region.
 *
 * The given region's lock MUST be held and next region's lock MUST NOT be held.
 */
static fpage_region_header_t* acquire_next_region(fpage_region_header_t* prev) {
	fpage_region_header_t* virt_prev = map_temporarily_auto(prev);
	fpage_region_header_t* next = virt_prev->next;
	if (next) {
		flock_spin_intsafe_lock(&((fpage_region_header_t*)map_temporarily_auto(next))->lock);
	}
	flock_spin_intsafe_unlock(&virt_prev->lock);
	return next;
};

/**
 * Like acquire_next_region(), but if the given region matches the given exception region, its lock is NOT released.
 */
static fpage_region_header_t* acquire_next_region_with_exception(fpage_region_header_t* prev, fpage_region_header_t* exception) {
	fpage_region_header_t* virt_prev = map_temporarily_auto(prev);
	fpage_region_header_t* next = virt_prev->next;
	if (next) {
		flock_spin_intsafe_lock(&((fpage_region_header_t*)map_temporarily_auto(next))->lock);
	}
	if (prev != exception) {
		flock_spin_intsafe_unlock(&virt_prev->lock);
	}
	return next;
};

/**
 * Allocates a physical frame of the given size.
 *
 * The ::regions_head lock and all the region locks MUST NOT be held.
 */
static void* allocate_frame(size_t page_count, uint8_t alignment_power, size_t* out_allocated_page_count) {
	// prefault now, before we acquire any locks
	fpage_prefault_stack(PREFAULT_PAGE_COUNT);

	if (alignment_power < FPAGE_MIN_ALIGNMENT) {
		alignment_power = FPAGE_MIN_ALIGNMENT;
	}

	uintptr_t alignment_mask = ((uintptr_t)1 << alignment_power) - 1;
	size_t min_order = min_order_for_page_count(page_count);

	fpage_region_header_t* candidate_parent_region = NULL;
	fpage_free_block_t* candidate_block = NULL;
	size_t candidate_order = FPAGE_MAX_ORDER;
	uintptr_t start_split = 0;

	fpage_free_block_t* aligned_candidate_block = NULL;
	size_t aligned_candidate_order = FPAGE_MAX_ORDER;

	// first, look for the smallest usable block from any region
	for (fpage_region_header_t* phys_region = acquire_first_region(); phys_region != NULL; phys_region = acquire_next_region_with_exception(phys_region, candidate_parent_region)) {
		fpage_region_header_t* region = map_temporarily_auto(phys_region);

		for (size_t order = min_order; order < FPAGE_MAX_ORDER && order < candidate_order; ++order) {
			fpage_free_block_t* phys_block = region->buckets[order];

			if (!phys_block) {
				continue;
			}

			if (((uintptr_t)phys_block & alignment_mask) != 0) {
				if (order > min_order) {
					// the start of this block isn't aligned the way we want;
					// let's see if a subblock within it is...
					uintptr_t next_aligned_address = ((uintptr_t)phys_block & ~alignment_mask) + (alignment_mask + 1);

					if (next_aligned_address > (uintptr_t)phys_block && next_aligned_address < (uintptr_t)phys_block + size_of_order(order)) {
						// okay great, the next aligned address falls within this block.
						// however, let's see if the subblock is big enough for us.
						uintptr_t block_end = (uintptr_t)phys_block + size_of_order(order);
						uintptr_t subblock = (uintptr_t)phys_block;
						size_t suborder = order - 1;
						bool found = false;

						while (suborder >= min_order && subblock < block_end) {
							if (((uintptr_t)subblock & alignment_mask) != 0) {
								// awesome, this subblock is big enough and it's aligned properly
								found = true;
								aligned_candidate_block = (void*)subblock;
								aligned_candidate_order = suborder;
								break;
							} else {
								if (next_aligned_address > (uintptr_t)subblock && next_aligned_address < (uintptr_t)subblock + size_of_order(suborder)) {
									// okay, so this subblock contains the address; let's search its subleaves
									if (suborder == min_order) {
										// can't split up a min order block to get an aligned block big enough
										break;
									} else {
										block_end = size_of_order(suborder);
										--suborder;
									}
								} else {
									// nope, this subblock doesn't contain the address; let's skip it
									subblock += size_of_order(suborder);
								}
							}
						}

						if (!found) {
							// none of this block's subblocks were big enough and aligned properly
							continue;
						}

						// great, we have an aligned subblock big enough; let's go ahead and save this candidate
					} else {
						// nope, the next aligned address isn't in this block.
						continue;
					}
				} else {
					// can't split up a min order block to get an aligned block big enough
					continue;
				}
			}

			if (phys_block) {
				if (candidate_parent_region) {
					flock_spin_intsafe_unlock(&((fpage_region_header_t*)map_temporarily_auto(candidate_parent_region))->lock);
				}
				candidate_order = order;
				candidate_block = phys_block;
				candidate_parent_region = phys_region;
				break;
			}
		}

		if (candidate_order == min_order) {
			// we can stop right here; we're not going to find a suitable block smaller than that!
			break;
		}
	}

	// uh-oh, we don't have any free blocks big enough in any region
	if (!candidate_block) {
		return NULL;
	}

	// the candidate parent region's lock is held here

	// okay, we've chosen our candidate region. un-free it
	remove_free_block(candidate_parent_region, candidate_block);
	frames_in_use += page_count_of_order(candidate_order);

	if (((uintptr_t)candidate_block & alignment_mask) != 0) {
		// alright, if we have an unaligned candidate block, we've already determined that
		// it does have an aligned subblock big enough for us, so let's split up the block to get it.

		uintptr_t block_end = (uintptr_t)candidate_block + size_of_order(candidate_order);
		uintptr_t subblock = (uintptr_t)candidate_block;
		size_t suborder = candidate_order - 1;

		while (suborder >= aligned_candidate_order) {
			uintptr_t next_subblock = 0;

			for (uintptr_t split_block = subblock; split_block < block_end; split_block += size_of_order(suborder)) {
				if ((uintptr_t)aligned_candidate_block >= (uintptr_t)subblock && (uintptr_t)aligned_candidate_block < (uintptr_t)subblock + size_of_order(suborder)) {
					// this block either is the aligned candidate block or contains the aligned candidate block
					next_subblock = split_block;
				} else {
					// this is just a block we don't care about; add it back to the region
					insert_free_block(candidate_parent_region, (void*)split_block, page_count_of_order(suborder));
				}
			}

			if (suborder == aligned_candidate_order) {
				// this is the order of the aligned candidate block, so this next subblock MUST be the aligned candidate block
				fassert(next_subblock == (uintptr_t)aligned_candidate_block);
				candidate_block = aligned_candidate_block;
				candidate_order = aligned_candidate_order;
				break;
			} else {
				// this is NOT the order of the aligned candidate block, so this MUST NOT be the aligned candidate block
				fassert(next_subblock != (uintptr_t)aligned_candidate_block);

				// now let's iterate through this block's subblocks
				subblock = next_subblock;
				block_end = subblock + size_of_order(suborder);
				--suborder;
			}
		}

		// the candidate block is now the aligned candidate block.
		// however, the aligned candidate block may have been too big for us,
		// so let's continue on with the usual shrinking/splitting case.
	}

	// we might have gotten a bigger block than we wanted. split it up.
	// the way this works can be illustrated like so:
	//
	// we found a block of 8 pages (order=3) when we only wanted 1 page (order=0).
	// 1. |               8               |
	// 2. | 1 |             7             | <- 1 is the page we want; initial state before iteration
	// 3. start iterating with order = 0 (which is min_order)
	// 4. | 1 | 1 |           6           | <- 1 is marked as free; order becomes 1
	// 5. | 1 | 1 |   2   |       4       | <- 2 is marked as free; order becomes 2
	// 6. | 1 | 1 |   2   |       4       | <- 4 is marked as free; order becomes 3
	// 7. stop iterating because order = 3 (which is candidate_order)
	start_split = (uintptr_t)candidate_block + (page_count_of_order(min_order) * FPAGE_PAGE_SIZE);
	for (size_t order = min_order; order < candidate_order; ++order) {
		fpage_free_block_t* phys_block = (void*)start_split;
		insert_free_block(candidate_parent_region, phys_block, page_count_of_order(order));
		start_split += (page_count_of_order(order) * FPAGE_PAGE_SIZE);
	}

	// alright, we now have the right-size block.

	// we can now release the parent region's lock
	flock_spin_intsafe_unlock(&((fpage_region_header_t*)map_temporarily_auto(candidate_parent_region))->lock);

	// ...let the user know how much we actually gave them (if they want to know that)...
	if (out_allocated_page_count) {
		*out_allocated_page_count = page_count_of_order(min_order);
	}

#if FPAGE_DEBUG_LOG_FRAMES
	if (fpage_logging_available) {
		fconsole_logf("Allocating frame %p (order = %zu)\n", candidate_block, min_order);
	}
#endif

	// ...and finally, give them their new block
	return candidate_block;
};

/**
 * Returns `true` if the given block belongs to the given region.
 *
 * The region's lock MUST be held.
 */
FERRO_ALWAYS_INLINE bool block_belongs_to_region(fpage_free_block_t* block, fpage_region_header_t* region) {
	region = map_temporarily_auto(region);
	return (uintptr_t)block >= (uintptr_t)region->start && (uintptr_t)block < (uintptr_t)region->start + (region->page_count * FPAGE_PAGE_SIZE);
};

/**
 * Frees a physical frame of the given size.
 *
 * The ::regions_head lock and all the region locks MUST NOT be held.
 */
static void free_frame(void* frame, size_t page_count) {
	// prefault now, before we acquire any locks
	fpage_prefault_stack(PREFAULT_PAGE_COUNT);

	size_t order = min_order_for_page_count(page_count);

#if FPAGE_DEBUG_LOG_FRAMES
	if (fpage_logging_available) {
		fconsole_logf("Freeing frame %p (order = %zu)\n", frame, order);
	}
#endif

	fpage_region_header_t* parent_region = NULL;
	fpage_free_block_t* block = frame;

	for (fpage_region_header_t* phys_region = acquire_first_region(); phys_region != NULL; phys_region = acquire_next_region_with_exception(phys_region, parent_region)) {
		if (block_belongs_to_region(block, phys_region)) {
			parent_region = phys_region;
			break;
		}
	}

	if (!parent_region) {
		fpanic("Freeing frame with no parent region");
	}

	if (!block_is_in_use(parent_region, block)) {
		fpanic("Attempt to free frame that wasn't allocated");
	}

	// parent region's lock is held here

	// find buddies to merge with
	for (; order < FPAGE_MAX_ORDER; ++order) {
		fpage_free_block_t* buddy = find_buddy(parent_region, block, page_count_of_order(order));
		bool correct_order = false;

		// oh, no buddy? how sad :(
		if (!buddy) {
			break;
		}

		if (block_is_in_use(parent_region, buddy)) {
			// whelp, our buddy is in use. we can't do any more merging
			break;
		}

		// make sure our buddy is of the order we're expecting
		for (fpage_free_block_t* maybe_buddy = map_temporarily_auto_type(parent_region)->buckets[order]; maybe_buddy != NULL; maybe_buddy = map_temporarily_auto_type(maybe_buddy)->next) {
			if (maybe_buddy == buddy) {
				// cool, it's the right order.
				correct_order = true;
				break;
			}
		}

		if (!correct_order) {
			// oh, looks like our buddy isn't the right size so we can't merge with them.
			break;
		}

		// yay, our buddy's free! let's get together.

		// take them out of their current bucket
		remove_free_block(parent_region, buddy);
		frames_in_use += page_count_of_order(order);

		// whoever's got the lower address is the start of the bigger block
		if (buddy < block) {
			block = buddy;
		}

		// now *don't* insert the new block into the free list.
		// that would be pointless since we might still have a buddy to merge with the bigger block
		// and we insert it later, after the loop.
	}

	// finally, insert the new (possibly merged) block into the appropriate bucket
	insert_free_block(parent_region, block, page_count_of_order(order));

	// we can now drop the lock
	flock_spin_intsafe_unlock(&((fpage_region_header_t*)map_temporarily_auto(parent_region))->lock);
};

//
// virtual memory allocator
//

static bool ensure_table(fpage_table_t* parent, size_t index) {
	if (!fpage_entry_is_active(parent->entries[index])) {
		fpage_table_t* table = allocate_frame(fpage_round_up_page(sizeof(fpage_table_t)) / FPAGE_PAGE_SIZE, 0, NULL);

		if (!table) {
			// oh no, looks like we don't have any more memory!
			return false;
		}

		simple_memset(map_temporarily_auto(table), 0, fpage_round_up_page(sizeof(fpage_table_t)));

		// table entries are marked as unprivileged; this is so that both privileged and unprivileged pages contained within them can be access properly.
		// the final entries (e.g. large page entries or L1 page table entries) should be marked with whatever privilege level they need.
		parent->entries[index] = fpage_entry_mark_privileged(fpage_table_entry((uintptr_t)table, true), false);
		fpage_synchronize_after_table_modification();
	}

	return true;
};

static bool space_ensure_table(fpage_space_t* space, fpage_table_t* phys_parent, size_t index, fpage_table_t** out_phys_child) {
	fpage_table_t* parent = map_temporarily_auto(phys_parent);
	if (!fpage_entry_is_active(parent->entries[index])) {
		fpage_table_t* table = allocate_frame(fpage_round_up_page(sizeof(fpage_table_t)) / FPAGE_PAGE_SIZE, 0, NULL);

		if (!table) {
			// oh no, looks like we don't have any more memory!
			return false;
		}

		simple_memset(map_temporarily_auto(table), 0, fpage_round_up_page(sizeof(fpage_table_t)));

		parent = map_temporarily_auto(phys_parent);

		// table entries are marked as unprivileged; this is so that both privileged and unprivileged pages contained within them can be access properly.
		// the final entries (e.g. large page entries or L1 page table entries) should be marked with whatever privilege level they need.
		parent->entries[index] = fpage_entry_mark_privileged(fpage_table_entry((uintptr_t)table, true), false);
		fpage_synchronize_after_table_modification();

		if (out_phys_child) {
			*out_phys_child = table;
		}

		if (space->active && phys_parent == space->l4_table) {
			// the address space is active and this is a new entry in the root table, so we need to mirror it in the root system table
			root_table->entries[index] = parent->entries[index];
		}
	} else {
		if (out_phys_child) {
			*out_phys_child = (void*)fpage_entry_address(parent->entries[index]);
		}
	}

	return true;
};

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

	table = map_temporarily_auto(space->l4_table);
	entry = table->entries[l4];

	// L4 table

	if (!fpage_entry_is_active(entry)) {
		return UINTPTR_MAX;
	}

	table = map_temporarily_auto((void*)fpage_entry_address(entry));
	entry = table->entries[l3];

	// L3 table

	if (!fpage_entry_is_active(entry)) {
		return UINTPTR_MAX;
	}

	if (fpage_entry_is_large_page_entry(entry)) {
		return fpage_entry_address(entry) | FPAGE_VIRT_VERY_LARGE_OFFSET(virtual_address);
	}

	table = map_temporarily_auto((void*)fpage_entry_address(entry));
	entry = table->entries[l2];

	// L2 table

	if (!fpage_entry_is_active(entry)) {
		return UINTPTR_MAX;
	}

	if (fpage_entry_is_large_page_entry(entry)) {
		return fpage_entry_address(entry) | FPAGE_VIRT_LARGE_OFFSET(virtual_address);
	}

	table = map_temporarily_auto((void*)fpage_entry_address(entry));
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
 * Like map_temporarily_auto(), addresses returned by calls to this function should not
 * be assumed to remain valid past most function calls. Only a select few known not to request temporary mappings
 * can be called without needing to remap temporarily-mapped addresses afterwards.
 */
FERRO_ALWAYS_INLINE void* space_map_temporarily_auto(fpage_space_t* space, void* virt) {
	uintptr_t phys = fpage_space_virtual_to_physical(space, (uintptr_t)virt);
	if (phys == UINTPTR_MAX) {
		fpanic("bad address within space");
		return NULL;
	}
	return map_temporarily_auto((void*)phys);
};

#define space_map_temporarily_auto_type(space, virt) ((__typeof__((virt)))space_map_temporarily_auto((space), (virt)))

static void free_table(fpage_table_t* table) {
	free_frame((void*)fpage_virtual_to_physical((uintptr_t)table), fpage_round_up_page(sizeof(fpage_table_t)) / FPAGE_PAGE_SIZE);
};

static void space_free_table(fpage_space_t* space, fpage_table_t* table) {
	free_frame((void*)fpage_space_virtual_to_physical(space, (uintptr_t)table), fpage_round_up_page(sizeof(fpage_table_t)) / FPAGE_PAGE_SIZE);
};

static void break_entry(size_t levels, size_t l4_index, size_t l3_index, size_t l2_index, size_t l1_index) {
	uintptr_t start_addr = fpage_make_virtual_address((levels > 0) ? l4_index : 0, (levels > 1) ? l3_index : 0, (levels > 2) ? l2_index : 0, (levels > 3) ? l1_index : 0, 0);
	uintptr_t end_addr = fpage_make_virtual_address((levels > 0) ? l4_index : TABLE_ENTRY_COUNT - 1, (levels > 1) ? l3_index : TABLE_ENTRY_COUNT - 1, (levels > 2) ? l2_index : TABLE_ENTRY_COUNT - 1, (levels > 3) ? l1_index : TABLE_ENTRY_COUNT - 1, 0xfff) + 1;

	// first, invalidate the entry
	if (levels == 0) {
		// invalidating the L4 table would be A Bad Thing (TM)
	} else {
		fpage_table_t* table = (fpage_table_t*)fpage_virtual_address_for_table(levels - 1, l4_index, l3_index, l2_index);
		size_t index = (levels < 2) ? l4_index : ((levels < 3) ? l3_index : ((levels < 4) ? l2_index : l1_index));

		table->entries[index] = 0;
		fpage_synchronize_after_table_modification();
	}

	// now invalidate TLB entries for all the addresses
	fpage_invalidate_tlb_for_range((void*)start_addr, (void*)end_addr);
	fpage_synchronize_after_table_modification();
};

FERRO_OPTIONS(fpage_flags_t, fpage_private_flags) {
	fpage_private_flag_inactive = 1ull << 63,
	fpage_private_flag_repeat   = 1ull << 62,
};

// NOTE: this function ***WILL*** overwrite existing entries!
static void space_map_frame_fixed(fpage_space_t* space, void* phys_frame, void* virt_frame, size_t page_count, fpage_private_flags_t flags) {
	uintptr_t physical_frame = (uintptr_t)phys_frame;
	uintptr_t virtual_frame = (uintptr_t)virt_frame;
	bool no_cache = (flags & fpage_flag_no_cache) != 0;
	bool unprivileged = (flags & fpage_flag_unprivileged) != 0;
	bool inactive = (flags & fpage_private_flag_inactive) != 0;
	bool repeat = (flags & fpage_private_flag_repeat) != 0;

	size_t orig_page_count = page_count;

	while (page_count > 0) {
		size_t l4_index = FPAGE_VIRT_L4(virtual_frame);
		size_t l3_index = FPAGE_VIRT_L3(virtual_frame);
		size_t l2_index = FPAGE_VIRT_L2(virtual_frame);
		size_t l1_index = FPAGE_VIRT_L1(virtual_frame);

		// L4 table

		fpage_table_t* phys_table = space->l4_table;
		fpage_table_t* table = map_temporarily_auto(phys_table);
		uint64_t entry = table->entries[l4_index];

		if (!space_ensure_table(space, phys_table, l4_index, &phys_table)) {
			return;
		}

		// L3 table

		table = map_temporarily_auto(phys_table);
		entry = table->entries[l3_index];

		if (fpage_is_very_large_page_aligned(physical_frame) && fpage_is_very_large_page_aligned(virtual_frame) && page_count >= FPAGE_VERY_LARGE_PAGE_COUNT) {
			if (!fpage_entry_is_large_page_entry(entry)) {
				// TODO: this doesn't free subtables
				space_free_table(space, (void*)fpage_entry_address(entry));
			}

			// break the existing entry
			if (space->active) {
				break_entry(2, l4_index, l3_index, 0, 0);
			}

			// now map our entry
			table = map_temporarily_auto(phys_table);
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

		if (fpage_entry_is_large_page_entry(entry) && space->active) {
			break_entry(2, l4_index, l3_index, 0, 0);

			// NOTE: this does not currently handle the case of partially remapping a large page
			//       e.g. if we want to map the first half to another location but keep the last half to where the large page pointed
			//       however, this is probably not something we'll ever want or need to do, so it's okay for now.
			//       just be aware of this limitation present here.
		}

		if (!space_ensure_table(space, phys_table, l3_index, &phys_table)) {
			return;
		}

		// L2 table

		table = map_temporarily_auto(phys_table);
		entry = table->entries[l2_index];

		if (fpage_is_large_page_aligned(physical_frame) && fpage_is_large_page_aligned(virtual_frame) && page_count >= FPAGE_LARGE_PAGE_COUNT) {
			if (!fpage_entry_is_large_page_entry(entry)) {
				// TODO: this doesn't free subtables
				space_free_table(space, (void*)fpage_entry_address(entry));
			}

			// break the existing entry
			if (space->active) {
				break_entry(3, l4_index, l3_index, l2_index, 0);
			}

			// now map our entry
			table = map_temporarily_auto(phys_table);
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

		if (fpage_entry_is_large_page_entry(entry) && space->active) {
			break_entry(3, l4_index, l3_index, l2_index, 0);

			// same note as for the l3 large page case
		}

		if (!space_ensure_table(space, phys_table, l2_index, &phys_table)) {
			return;
		}

		// L1 table

		table = map_temporarily_auto(phys_table);
		entry = table->entries[l1_index];

		if (entry && space->active) {
			break_entry(4, l4_index, l3_index, l2_index, l1_index);
		}

		table = map_temporarily_auto(phys_table);
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

	// now flush the region (in case we're replacing an existing mapping)
	// TODO: we can optimize this by doing the flushing directly within the loop above
	fpage_space_flush_mapping_internal(space, virt_frame, orig_page_count, space->active, false, false);
};

FERRO_ALWAYS_INLINE size_t space_virtual_bitmap_bit_index_for_block(fpage_space_t* space, const fpage_region_header_t* space_parent_region, const fpage_free_block_t* space_block) {
	uintptr_t relative_address = 0;
	const fpage_region_header_t* parent_region_temp = space_map_temporarily_auto(space, (void*)space_parent_region);
	relative_address = (uintptr_t)space_block - (uintptr_t)parent_region_temp->start;
	return relative_address / FPAGE_PAGE_SIZE;
};

FERRO_ALWAYS_INLINE size_t virtual_byte_index_for_bit(size_t bit_index) {
	return bit_index / 8;
};

FERRO_ALWAYS_INLINE size_t virtual_byte_bit_index_for_bit(size_t bit_index) {
	return bit_index % 8;
};

/**
 * @note The address returned is temporarily mapped.
 */
static const uint8_t* space_virtual_bitmap_entry_for_block(fpage_space_t* space, const fpage_region_header_t* space_parent_region, const fpage_free_block_t* space_block, size_t* out_bit_index) {
	size_t bitmap_index = space_virtual_bitmap_bit_index_for_block(space, space_parent_region, space_block);
	size_t byte_index = virtual_byte_index_for_bit(bitmap_index);
	size_t byte_bit_index = virtual_byte_bit_index_for_bit(bitmap_index);
	const uint8_t* byte = NULL;

	byte = space_map_temporarily_auto(space, (void*)&space_parent_region->bitmap[byte_index]);
	*out_bit_index = byte_bit_index;

	return byte;
};

static bool space_virtual_block_is_in_use(fpage_space_t* space, const fpage_region_header_t* space_parent_region, const fpage_free_block_t* space_block) {
	size_t byte_bit_index = 0;
	const uint8_t* byte = space_virtual_bitmap_entry_for_block(space, space_parent_region, space_block, &byte_bit_index);

	return (*byte) & (1 << byte_bit_index);
};

static void space_set_virtual_block_is_in_use(fpage_space_t* space, fpage_region_header_t* space_parent_region, const fpage_free_block_t* space_block, bool in_use) {
	size_t byte_bit_index = 0;
	uint8_t* byte = (uint8_t*)space_virtual_bitmap_entry_for_block(space, space_parent_region, space_block, &byte_bit_index);

	if (in_use) {
		*byte |= 1 << byte_bit_index;
	} else {
		*byte &= ~(1 << byte_bit_index);
	}
};

#if FPAGE_SPACE_CHECK_REGIONS
// we rarely merge blocks larger than this order, so there's no real need to check them
#define FPAGE_MAX_CHECK_ORDER (FPAGE_MAX_ORDER / 2)

/**
 * Checks whether all the blocks in the region are valid.
 * Used for debugging.
 *
 * @note This a VERY expensive call (it's `O(n^2)`, with n being the number of blocks in the region).
 *
 * @pre The region's lock MUST be held.
 */
FERRO_NO_OPTIMIZE
static void fpage_space_region_check_blocks(fpage_space_t* space, fpage_region_header_t* region) {
	for (size_t order = 0; order < FPAGE_MAX_CHECK_ORDER; ++order) {
		size_t size = page_count_of_order(order) * FPAGE_PAGE_SIZE;

		for (fpage_free_block_t* block = space_map_temporarily_auto_type(space, region)->buckets[order]; block != NULL; block = space_map_temporarily_auto_type(space, block)->next) {
			if (!space_map_temporarily_auto_type(space, block)->prev) {
				fpanic("Invalid block (no prev value)");
			}

			void* block_start = block;
			void* block_end = (char*)block + size;

			// check that it doesn't overlap with any free blocks
			for (size_t order2 = 0; order2 < FPAGE_MAX_CHECK_ORDER; ++order2) {
				size_t size2 = page_count_of_order(order2) * FPAGE_PAGE_SIZE;

				for (fpage_free_block_t* block2 = space_map_temporarily_auto_type(space, region)->buckets[order2]; block2 != NULL; block2 = space_map_temporarily_auto_type(space, block2)->next) {
					if (block == block2) {
						continue;
					}

					if (!space_map_temporarily_auto_type(space, block2)->prev) {
						fpanic("Invalid block (no prev value)");
					}

					void* block2_start = block2;
					void* block2_end = (char*)block2 + size2;

					if (
						(block_start <= block2_start && block_end > block2_start) ||
						(block2_start <= block_start && block2_end > block_start)
					) {
						fpanic("Overlapping blocks");
					}
				}
			}

			// now check that it doesn't overlap with any used blocks
			for (size_t i = 0; i < page_count_of_order(order); ++i) {
				if (space_virtual_block_is_in_use(space, region, (void*)((char*)block + (i * FPAGE_PAGE_SIZE)))) {
					fpanic("Free block has in-use subblocks");
				}
			}
		}
	}
};
#endif

static void space_insert_virtual_free_block(fpage_space_t* space, fpage_region_header_t* parent_region, fpage_free_block_t* space_block, size_t block_page_count) {
	size_t order = max_order_of_page_count(block_page_count);
	fpage_free_block_t* phys_block = allocate_frame(fpage_round_up_page(sizeof(fpage_free_block_t)) / FPAGE_PAGE_SIZE, 0, NULL);

	if (!phys_block) {
		fpanic("failed to allocate physical block for virtual free block");
	}

	space_map_frame_fixed(space, phys_block, space_block, fpage_round_up_page(sizeof(fpage_free_block_t)) / FPAGE_PAGE_SIZE, 0);

	fpage_region_header_t* virt_parent_region = space_map_temporarily_auto(space, parent_region);
	fpage_free_block_t* block_temp = map_temporarily_auto(phys_block);

	block_temp->prev = &parent_region->buckets[order];
	block_temp->next = virt_parent_region->buckets[order];

	if (block_temp->next) {
		space_map_temporarily_auto_type(space, block_temp->next)->prev = &space_block->next;
	}

	virt_parent_region->buckets[order] = space_block;

	space_set_virtual_block_is_in_use(space, parent_region, space_block, false);

#if FPAGE_SPACE_CHECK_REGIONS
	fpage_space_region_check_blocks(space, parent_region);
#endif
};

static void space_remove_virtual_free_block(fpage_space_t* space, fpage_region_header_t* parent_region, fpage_free_block_t* space_block) {
	fpage_free_block_t* block_temp = space_map_temporarily_auto(space, space_block);

	*space_map_temporarily_auto_type(space, block_temp->prev) = block_temp->next;
	if (block_temp->next) {
		space_map_temporarily_auto_type(space, block_temp->next)->prev = block_temp->prev;
	}

	free_frame((void*)fpage_space_virtual_to_physical(space, (uintptr_t)space_block), fpage_round_up_page(sizeof(fpage_free_block_t)) / FPAGE_PAGE_SIZE);

	fpage_space_flush_mapping_internal(space, space_block, fpage_round_up_to_page_count(sizeof(fpage_free_block_t)), space->active, true, false);

#if FPAGE_SPACE_CHECK_REGIONS
	fpage_space_region_check_blocks(space, parent_region);
#endif
};

static fpage_free_block_t* space_find_virtual_buddy(fpage_space_t* space, fpage_region_header_t* space_parent_region, fpage_free_block_t* space_block, size_t block_page_count) {
	fpage_region_header_t* parent_region_temp = space_map_temporarily_auto(space, space_parent_region);
	uintptr_t parent_start = (uintptr_t)parent_region_temp->start;
	uintptr_t maybe_buddy = (((uintptr_t)space_block - parent_start) ^ (block_page_count * FPAGE_PAGE_SIZE)) + parent_start;

	if (maybe_buddy + (block_page_count * FPAGE_PAGE_SIZE) > parent_start + (parent_region_temp->page_count * FPAGE_PAGE_SIZE)) {
		return NULL;
	}

	return (fpage_free_block_t*)maybe_buddy;
};

static fpage_region_header_t* space_virtual_acquire_first_region(fpage_space_t* space) {
	fpage_region_header_t* region;

	flock_spin_intsafe_lock(&space->regions_head_lock);
	region = space->regions_head;
	if (region) {
		flock_spin_intsafe_lock(space_map_temporarily_auto(space, &region->lock));
	}
	flock_spin_intsafe_unlock(&space->regions_head_lock);
	return region;
};

static fpage_region_header_t* space_virtual_acquire_next_region(fpage_space_t* space, fpage_region_header_t* prev) {
	fpage_region_header_t* prev_temp = space_map_temporarily_auto(space, prev);
	fpage_region_header_t* next = prev_temp->next;
	if (next) {
		flock_spin_intsafe_lock(space_map_temporarily_auto(space, &next->lock));
	}
	flock_spin_intsafe_unlock(&prev_temp->lock);
	return next;
};

static fpage_region_header_t* space_virtual_acquire_next_region_with_exception(fpage_space_t* space, fpage_region_header_t* prev, fpage_region_header_t* exception) {
	fpage_region_header_t* prev_temp = space_map_temporarily_auto(space, prev);
	fpage_region_header_t* next = prev_temp->next;
	if (next) {
		flock_spin_intsafe_lock(space_map_temporarily_auto(space, &next->lock));
	}
	if (prev != exception) {
		flock_spin_intsafe_unlock(&prev_temp->lock);
	}
	return next;
};

/**
 * Allocates a virtual region of the given size in the given address space.
 *
 * The region head lock and all the region locks MUST NOT be held.
 */
static void* space_allocate_virtual(fpage_space_t* space, size_t page_count, uint8_t alignment_power, size_t* out_allocated_page_count, bool user) {
	if (alignment_power < FPAGE_MIN_ALIGNMENT) {
		alignment_power = FPAGE_MIN_ALIGNMENT;
	}

	uintptr_t alignment_mask = ((uintptr_t)1 << alignment_power) - 1;
	size_t min_order = min_order_for_page_count(page_count);

	fpage_region_header_t* space_candidate_parent_region = NULL;
	fpage_free_block_t* space_candidate_block = NULL;
	size_t candidate_order = FPAGE_MAX_ORDER;
	uintptr_t start_split = 0;

	fpage_free_block_t* aligned_candidate_block = NULL;
	size_t aligned_candidate_order = FPAGE_MAX_ORDER;

	// first, look for the smallest usable block from any region
	for (fpage_region_header_t* space_region = space_virtual_acquire_first_region(space); space_region != NULL; space_region = space_virtual_acquire_next_region_with_exception(space, space_region, space_candidate_parent_region)) {
		for (size_t order = min_order; order < FPAGE_MAX_ORDER && order < candidate_order; ++order) {
			fpage_free_block_t* block = space_map_temporarily_auto_type(space, space_region)->buckets[order];

			if (!block) {
				continue;
			}

			if (((uintptr_t)block & alignment_mask) != 0) {
				if (order > min_order) {
					// the start of this block isn't aligned the way we want;
					// let's see if a subblock within it is...
					uintptr_t next_aligned_address = ((uintptr_t)block & ~alignment_mask) + (alignment_mask + 1);

					if (next_aligned_address > (uintptr_t)block && next_aligned_address < (uintptr_t)block + size_of_order(order)) {
						// okay great, the next aligned address falls within this block.
						// however, let's see if the subblock is big enough for us.
						uintptr_t block_end = (uintptr_t)block + size_of_order(order);
						uintptr_t subblock = (uintptr_t)block;
						size_t suborder = order - 1;
						bool found = false;

						while (suborder >= min_order && subblock < block_end) {
							if (((uintptr_t)subblock & alignment_mask) != 0) {
								// awesome, this subblock is big enough and it's aligned properly
								found = true;
								aligned_candidate_block = (void*)subblock;
								aligned_candidate_order = suborder;
								break;
							} else {
								if (next_aligned_address > (uintptr_t)subblock && next_aligned_address < (uintptr_t)subblock + size_of_order(suborder)) {
									// okay, so this subblock contains the address; let's search its subleaves
									if (suborder == min_order) {
										// can't split up a min order block to get an aligned block big enough
										break;
									} else {
										block_end = size_of_order(suborder);
										--suborder;
									}
								} else {
									// nope, this subblock doesn't contain the address; let's skip it
									subblock += size_of_order(suborder);
								}
							}
						}

						if (!found) {
							// none of this block's subblocks were big enough and aligned properly
							continue;
						}

						// great, we have an aligned subblock big enough; let's go ahead and save this candidate
					} else {
						// nope, the next aligned address isn't in this block.
						continue;
					}
				} else {
					// can't split up a min order block to get an aligned block big enough
					continue;
				}
			}

			if (block) {
				if (space_candidate_parent_region) {
					flock_spin_intsafe_unlock(space_map_temporarily_auto(space, &space_candidate_parent_region->lock));
				}
				candidate_order = order;
				space_candidate_block = block;
				space_candidate_parent_region = space_region;
				break;
			}
		}

		if (candidate_order == min_order) {
			break;
		}
	}

	// uh-oh, we don't have any free blocks big enough in any region
	if (!space_candidate_block) {
		return NULL;
	}

	// the candidate parent region's lock is held here

	// okay, we've chosen our candidate region. un-free it
	space_remove_virtual_free_block(space, space_candidate_parent_region, space_candidate_block);

	if (((uintptr_t)space_candidate_block & alignment_mask) != 0) {
		// alright, if we have an unaligned candidate block, we've already determined that
		// it does have an aligned subblock big enough for us, so let's split up the block to get it.

		uintptr_t block_end = (uintptr_t)space_candidate_block + size_of_order(candidate_order);
		uintptr_t subblock = (uintptr_t)space_candidate_block;
		size_t suborder = candidate_order - 1;

		while (suborder >= aligned_candidate_order) {
			uintptr_t next_subblock = 0;

			for (uintptr_t split_block = subblock; split_block < block_end; split_block += size_of_order(suborder)) {
				if ((uintptr_t)aligned_candidate_block >= (uintptr_t)subblock && (uintptr_t)aligned_candidate_block < (uintptr_t)subblock + size_of_order(suborder)) {
					// this block either is the aligned candidate block or contains the aligned candidate block
					next_subblock = split_block;
				} else {
					// this is just a block we don't care about; add it back to the region
					space_insert_virtual_free_block(space, space_candidate_parent_region, (void*)split_block, page_count_of_order(suborder));
				}
			}

			if (suborder == aligned_candidate_order) {
				// this is the order of the aligned candidate block, so this next subblock MUST be the aligned candidate block
				fassert(next_subblock == (uintptr_t)aligned_candidate_block);
				space_candidate_block = aligned_candidate_block;
				candidate_order = aligned_candidate_order;
				break;
			} else {
				// this is NOT the order of the aligned candidate block, so this MUST NOT be the aligned candidate block
				fassert(next_subblock != (uintptr_t)aligned_candidate_block);

				// now let's iterate through this block's subblocks
				subblock = next_subblock;
				block_end = subblock + size_of_order(suborder);
				--suborder;
			}
		}

		// the candidate block is now the aligned candidate block.
		// however, the aligned candidate block may have been too big for us,
		// so let's continue on with the usual shrinking/splitting case.
	}

	// we might have gotten a bigger block than we wanted. split it up.
	// to understand how this works, see allocate_frame().
	start_split = (uintptr_t)space_candidate_block + (page_count_of_order(min_order) * FPAGE_PAGE_SIZE);
	for (size_t order = min_order; order < candidate_order; ++order) {
		fpage_free_block_t* block = (void*)start_split;
		space_insert_virtual_free_block(space, space_candidate_parent_region, block, page_count_of_order(order));
		start_split += (page_count_of_order(order) * FPAGE_PAGE_SIZE);
	}

	// alright, we now have the right-size block.

	// now let's mark it as in-use
	space_set_virtual_block_is_in_use(space, space_candidate_parent_region, space_candidate_block, true);

	// drop the parent region lock
	flock_spin_intsafe_unlock(space_map_temporarily_auto(space, &space_candidate_parent_region->lock));

	// ...let the user know how much we actually gave them (if they want to know that)...
	if (out_allocated_page_count) {
		*out_allocated_page_count = page_count_of_order(min_order);
	}

	// ...and finally, give them their new block
	return space_candidate_block;
};

FERRO_ALWAYS_INLINE bool space_virtual_block_belongs_to_region(fpage_space_t* space, fpage_free_block_t* space_block, fpage_region_header_t* space_region) {
	fpage_region_header_t* region_temp = space_map_temporarily_auto(space, space_region);
	return (uintptr_t)space_block >= (uintptr_t)region_temp->start && (uintptr_t)space_block < (uintptr_t)region_temp->start + (region_temp->page_count * FPAGE_PAGE_SIZE);
};

static bool space_region_belongs_to_buddy_allocator(fpage_space_t* space, void* virtual_start, size_t page_count) {
	void* virtual_end = (void*)(fpage_round_down_page((uintptr_t)virtual_start) + (page_count * FPAGE_PAGE_SIZE));

	for (fpage_region_header_t* space_region = space_virtual_acquire_first_region(space); space_region != NULL; space_region = space_virtual_acquire_next_region(space, space_region)) {
		fpage_region_header_t* region_temp = space_map_temporarily_auto(space, space_region);
		void* region_start = region_temp->start;
		void* region_end = (void*)((uintptr_t)region_start + (region_temp->page_count * FPAGE_PAGE_SIZE));

		if (virtual_start < region_end && virtual_end > region_start) {
			flock_spin_intsafe_unlock(&space_region->lock);
			return true;
		}
	}

	return false;
};

static bool space_free_virtual(fpage_space_t* space, void* virtual, size_t page_count, bool user) {
	size_t order = min_order_for_page_count(page_count);

	fpage_region_header_t* space_parent_region = NULL;
	fpage_free_block_t* space_block = virtual;

	for (fpage_region_header_t* space_region = space_virtual_acquire_first_region(space); space_region != NULL; space_region = space_virtual_acquire_next_region_with_exception(space, space_region, space_parent_region)) {
		if (space_virtual_block_belongs_to_region(space, space_block, space_region)) {
			space_parent_region = space_region;
			break;
		}
	}

	if (!space_parent_region) {
		return false;
	}

	// the parent region's lock is held here

	// find buddies to merge with
	for (; order < FPAGE_MAX_ORDER; ++order) {
		fpage_free_block_t* buddy = space_find_virtual_buddy(space, space_parent_region, space_block, page_count_of_order(order));
		bool correct_order = false;

		// oh, no buddy? how sad :(
		if (!buddy) {
			break;
		}

		if (space_virtual_block_is_in_use(space, space_parent_region, buddy)) {
			// whelp, our buddy is in use. we can't do any more merging
			break;
		}

		// make sure our buddy is of the order we're expecting
		for (fpage_free_block_t* maybe_buddy = space_map_temporarily_auto_type(space, space_parent_region)->buckets[order]; maybe_buddy != NULL; maybe_buddy = space_map_temporarily_auto_type(space, maybe_buddy)->next) {
			if (maybe_buddy == buddy) {
				// cool, it's the right order.
				correct_order = true;
				break;
			}
		}

		if (!correct_order) {
			// oh, looks like our buddy isn't the right size so we can't merge with them.
			break;
		}

		// yay, our buddy's free! let's get together.

		// take them out of their current bucket
		space_remove_virtual_free_block(space, space_parent_region, buddy);

		// whoever's got the lower address is the start of the bigger block
		if (buddy < space_block) {
			space_block = buddy;
		}

		// now *don't* insert the new block into the free list.
		// that would be pointless since we might still have a buddy to merge with the bigger block
		// and we insert it later, after the loop.
	}

	// finally, insert the new (possibly merged) block into the appropriate bucket
	space_insert_virtual_free_block(space, space_parent_region, space_block, page_count_of_order(order));

	// drop the parent region's lock
	flock_spin_intsafe_unlock(space_map_temporarily_auto(space, &space_parent_region->lock));

	return true;
};

static size_t total_phys_page_count = 0;

// we don't need to worry about locks in this function; interrupts are disabled and we're in a uniprocessor environment
void fpage_init(size_t next_l2, fpage_table_t* table, ferro_memory_region_t* memory_regions, size_t memory_region_count, void* image_base) {
	fpage_table_t* l2_table = NULL;
	uintptr_t virt_start = FERRO_KERNEL_VIRTUAL_START;
	size_t max_virt_page_count = 0;
	size_t total_virt_page_count = 0;

	// initialize the address space pointer with the kernel address space
	*fpage_space_current_pointer() = &kernel_address_space;

	root_table = table;
	kernel_l4_index = FPAGE_VIRT_L4(FERRO_KERNEL_VIRTUAL_START);
	kernel_l3_index = FPAGE_VIRT_L3(FERRO_KERNEL_VIRTUAL_START);

	// determine the correct recursive index
	while (root_table->entries[root_recursive_index] != 0) {
		--root_recursive_index;

		if (root_recursive_index == 0) {
			// well, crap. we can't go lower than 0. just overwrite whatever's at 0.
			break;
		}
	}

	// set up the recursive mapping
	// can't use fpage_virtual_to_physical() for the physical address lookup because it depends on the recursive entry (which is what we're setting up right now).
	//
	// this should remain a privileged table, so that unprivileged code can't modify page tables willy-nilly
	root_table->entries[root_recursive_index] = fpage_table_entry(FERRO_KERNEL_STATIC_TO_OFFSET(root_table) + (uintptr_t)image_base, true);
	fpage_synchronize_after_table_modification();

	// we can use the recursive virtual address for the table now
	root_table = (fpage_table_t*)fpage_virtual_address_for_table(0, 0, 0, 0);

#if USE_TEMPORARY_MAPPING
	// set up the temporary table
	l2_table = (fpage_table_t*)fpage_virtual_address_for_table(2, FPAGE_VIRT_L4(FERRO_KERNEL_VIRTUAL_START), FPAGE_VIRT_L3(FERRO_KERNEL_VIRTUAL_START), 0);
	// likewise, this remains privileged so that unprivileged code can't temporary map memory as it wishes
	l2_table->entries[temporary_table_index = next_l2] = fpage_table_entry(fpage_virtual_to_physical((uintptr_t)&temporary_table), true);
	fpage_synchronize_after_table_modification();
#else
	// map all the physical memory at a fixed offset.
	// we assume it's 512GiB or less; no consumer device supports more than 128GiB currently.
	// we can always add more later.

	// determine the correct offset index
	while (root_table->entries[root_offset_index] != 0) {
		--root_offset_index;

		if (root_offset_index == 0) {
			// well, crap. we can't go lower than 0. just overwrite whatever's at 0.
			break;
		}
	}

	for (size_t i = 0; i < TABLE_ENTRY_COUNT; ++i) {
		offset_table.entries[i] = fpage_very_large_page_entry(i * FPAGE_VERY_LARGE_PAGE_SIZE, true);
	}

	// this also remains a privileged table so that unprivileged code can't access physical memory directly
	root_table->entries[root_offset_index] = fpage_table_entry(fpage_virtual_to_physical((uintptr_t)&offset_table), true);
	fpage_synchronize_after_table_modification();
#endif

	// okay, now we need to initialize each physical region

	for (size_t i = 0; i < memory_region_count; ++i) {
		const ferro_memory_region_t* region = &memory_regions[i];
		size_t pages_allocated = 0;
		size_t page_count = region->page_count;
		uintptr_t physical_start = region->physical_start;
		size_t bitmap_byte_count = 0;
		fpage_region_header_t* header = NULL;
		void* usable_start = NULL;
		size_t extra_bitmap_page_count = 0;

		// skip non-general memory
		if (region->type != ferro_memory_region_type_general) {
			continue;
		}

		// 0 == NULL
		// since it's a special address, we don't want to use it at all.
		// skip the first page if this is the case.
		if (physical_start == 0) {
			--page_count;
			physical_start += FPAGE_PAGE_SIZE;
		}

		if (page_count == 0) {
			continue;
		}

		// we need at least one page for the header
		--page_count;

		// not large enough
		if (page_count == 0) {
			continue;
		}

		// we might need more for the bitmap
		// divide by 8 because each page is represented by a bit
		bitmap_byte_count = (page_count + 7) / 8;

		// figure out if we need more space for the bitmap than what's left over from the header
		if (bitmap_byte_count >= HEADER_BITMAP_SPACE) {
			// extra pages are required for the bitmap
			extra_bitmap_page_count = fpage_round_up_page(bitmap_byte_count - HEADER_BITMAP_SPACE) / FPAGE_PAGE_SIZE;
			if (extra_bitmap_page_count >= page_count) {
				continue;
			}
			page_count -= extra_bitmap_page_count;
		}

		// okay, we're definitely going to use this region
		header = map_temporarily_auto((void*)physical_start);
		header->prev = (fpage_region_header_t**)fpage_virtual_to_physical((uintptr_t)&regions_head);
		header->next = regions_head;
		if (header->next) {
			fpage_region_header_t* previous_head = map_temporarily_auto(header->next);
			previous_head->prev = &((fpage_region_header_t*)physical_start)->next;
		}
		header->page_count = page_count;
		header->start = usable_start = (void*)(region->physical_start + ((region->page_count - page_count) * FPAGE_PAGE_SIZE));

		flock_spin_intsafe_init(&header->lock);

		regions_head = (fpage_region_header_t*)physical_start;
		total_phys_page_count += page_count;

		// clear out the bitmap
		simple_memset(&header->bitmap[0], 0, HEADER_BITMAP_SPACE);
		for (size_t i = 0; i < extra_bitmap_page_count; ++i) {
			uint8_t* page = (uint8_t*)map_temporarily_auto((void*)(physical_start + FPAGE_PAGE_SIZE + (i * FPAGE_PAGE_SIZE)));
			simple_memset(page, 0, FPAGE_PAGE_SIZE);
		}

		// clear out the buckets
		simple_memset(&header->buckets[0], 0, sizeof(header->buckets));

		while (pages_allocated < page_count) {
			size_t order = max_order_of_page_count(page_count - pages_allocated);
			size_t pages = page_count_of_order(order);
			void* phys_addr = (void*)((uintptr_t)usable_start + (pages_allocated * FPAGE_PAGE_SIZE));

			insert_free_block((void*)physical_start, phys_addr, pages);

			pages_allocated += pages;
		}
	}

	// initialize the frames-in-use counter to 0
	frames_in_use = 0;

	// next we need to enumerate and set up available virtual memory regions
	//
	// for now, we only need to set up the kernel address space

	// also, determine the maximum amount of virtual memory the buddy allocator can use.
	// this is based on `total_phys_page_count`, which is the total amount of *usable* physical memory
	// (i.e. we might have more than `total_phys_page_count`, but it's unusable).
	max_virt_page_count = total_phys_page_count * MAX_VIRTUAL_KERNEL_BUDDY_ALLOCATOR_PAGE_COUNT_COEFFICIENT;

	// address spaces store *physical* addresses, not virtual ones
	kernel_address_space.l4_table = (void*)fpage_virtual_to_physical((uintptr_t)kernel_address_space.l4_table);

	// initialize the kernel address space root table with the root table
	// TODO: we can skip copying the temporary identity mapping entries, they're no longer necessary
	simple_memcpy(&kernel_address_space_root_table, root_table, sizeof(kernel_address_space_root_table));

	// once we reach the maximum, it'll wrap around to 0
	while (virt_start != 0) {
		size_t virt_page_count = 0;
		size_t bitmap_byte_count = 0;
		size_t extra_bitmap_page_count = 0;
		size_t l4_index = FPAGE_VIRT_L4(virt_start);
		size_t l3_index = FPAGE_VIRT_L3(virt_start);
		size_t l2_index = FPAGE_VIRT_L2(virt_start);
		size_t l1_index = FPAGE_VIRT_L1(virt_start);
		fpage_table_t* l4 = (fpage_table_t*)fpage_virtual_address_for_table(0, 0, 0, 0);
		fpage_region_header_t* phys_header = NULL;
		fpage_region_header_t* header = NULL;
		bool failed_to_allocate_bitmap = false;
		void* usable_start = NULL;
		size_t pages_allocated = 0;

		// find the first free address

		for (; l4_index < TABLE_ENTRY_COUNT; ++l4_index) {
			fpage_table_t* l3 = (fpage_table_t*)fpage_virtual_address_for_table(1, l4_index, 0, 0);

			// don't touch the recursive entry or the offset index
			if (l4_index == root_recursive_index
#if !USE_TEMPORARY_MAPPING
				|| l4_index == root_offset_index
#endif
			) {
				continue;
			}

			// if the l4 entry is inactive, it's free! otherwise, we need to check further.
			if (!fpage_entry_is_active(l4->entries[l4_index])) {
				l3_index = 0;
				l2_index = 0;
				l1_index = 0;
				goto determine_size;
			}

			for (; l3_index < TABLE_ENTRY_COUNT; ++l3_index) {
				fpage_table_t* l2 = (fpage_table_t*)fpage_virtual_address_for_table(2, l4_index, l3_index, 0);

				// ditto
				if (!fpage_entry_is_active(l3->entries[l3_index])) {
					l2_index = 0;
					l1_index = 0;
					goto determine_size;
				}

				// we know that any address covered by the large page entry is not free, so try again on the next index
				if (fpage_entry_is_large_page_entry(l3->entries[l3_index])) {
					continue;
				}

				for (; l2_index < TABLE_ENTRY_COUNT; ++l2_index) {
					fpage_table_t* l1 = (fpage_table_t*)fpage_virtual_address_for_table(3, l4_index, l3_index, l2_index);

					if (!fpage_entry_is_active(l2->entries[l2_index])) {
						goto determine_size;
					}

					// ditto
					if (fpage_entry_is_large_page_entry(l2->entries[l2_index])) {
						l1_index = 0;
						continue;
					}

#if USE_TEMPORARY_MAPPING
					// skip the temporary table
					if (l4_index == kernel_l4_index && l3_index == kernel_l3_index && l2_index == temporary_table_index) {
						l1_index = 0;
						continue;
					}
#endif

					for (; l1_index < TABLE_ENTRY_COUNT; ++l1_index) {
						if (!fpage_entry_is_active(l1->entries[l1_index])) {
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
			fpage_table_t* l3 = (fpage_table_t*)fpage_virtual_address_for_table(1, l4_index, 0, 0);

			// not active? great, we've got an entire 512GiB region free!
			if (!fpage_entry_is_active(l4->entries[l4_index])) {
				virt_page_count += TABLE_ENTRY_COUNT * TABLE_ENTRY_COUNT * TABLE_ENTRY_COUNT;
				l3_index = 0;
				l2_index = 0;
				l1_index = 0;
				continue;
			}

			for (; l3_index < TABLE_ENTRY_COUNT; ++l3_index) {
				fpage_table_t* l2 = (fpage_table_t*)fpage_virtual_address_for_table(2, l4_index, l3_index, 0);

				// again: not active? awesome, we've got an entire 1GiB region free!
				if (!fpage_entry_is_active(l3->entries[l3_index])) {
					virt_page_count += TABLE_ENTRY_COUNT * TABLE_ENTRY_COUNT;
					l2_index = 0;
					l1_index = 0;
					continue;
				}

				// we know that any address covered by the large page entry is not free, so we're done
				if (fpage_entry_is_large_page_entry(l3->entries[l3_index])) {
					goto done_determining_size;
				}

				for (; l2_index < TABLE_ENTRY_COUNT; ++l2_index) {
					fpage_table_t* l1 = (fpage_table_t*)fpage_virtual_address_for_table(3, l4_index, l3_index, l2_index);

					// once again: not active? neat, we've got a 2MiB region free!
					if (!fpage_entry_is_active(l2->entries[l2_index])) {
						l1_index = 0;
						virt_page_count += TABLE_ENTRY_COUNT;
						continue;
					}

					// ditto: large page == non-free; we're done
					if (fpage_entry_is_large_page_entry(l2->entries[l2_index])) {
						goto done_determining_size;
					}

#if USE_TEMPORARY_MAPPING
					// the temporary table counts as non-free
					if (l4_index == kernel_l4_index && l3_index == kernel_l3_index && l2_index == temporary_table_index) {
						goto done_determining_size;
					}
#endif

					for (; l1_index < TABLE_ENTRY_COUNT; ++l1_index) {
						// not active? cool, we've got a free page.
						if (!fpage_entry_is_active(l1->entries[l1_index])) {
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

		// we need at least one page for the header
		--virt_page_count;

		// not large enough
		if (virt_page_count == 0) {
			virt_start += FPAGE_PAGE_SIZE;
			continue;
		}

		// make sure we don't try to use more than the maximum
		if (total_virt_page_count + virt_page_count >= max_virt_page_count) {
			// yes, doing this here means that the bitmap pages reduce the actual number of usable virtual pages for the buddy allocator,
			// but in practice, this difference is insignificant
			virt_page_count = max_virt_page_count - total_virt_page_count;
		}

		// we might need more for the bitmap
		// divide by 8 because each page is represented by a bit
		bitmap_byte_count = (virt_page_count + 7) / 8;

		// figure out if we need more space for the bitmap than what's left over from the header
		if (bitmap_byte_count >= HEADER_BITMAP_SPACE) {
			// extra pages are required for the bitmap
			extra_bitmap_page_count = fpage_round_up_page(bitmap_byte_count - HEADER_BITMAP_SPACE) / FPAGE_PAGE_SIZE;

			// not enough pages? skip this region
			if (extra_bitmap_page_count >= virt_page_count) {
				virt_start += FPAGE_PAGE_SIZE + virt_page_count;
				continue;
			}

			virt_page_count -= extra_bitmap_page_count;
		}

		// okay, we're definitely going to use this region

		phys_header = allocate_frame(fpage_round_up_page(sizeof(fpage_region_header_t)) / FPAGE_PAGE_SIZE, 0, NULL);

		if (!phys_header) {
			// crap. we're out of physical memory.
			// it's unlikely we'll be able to satisfy future requests, but skip this region and continue with the next anyways.
			virt_start += (1 + virt_page_count + extra_bitmap_page_count) * FPAGE_PAGE_SIZE;
			continue;
		}

		header = (fpage_region_header_t*)virt_start;

		space_map_frame_fixed(&kernel_address_space, phys_header, header, fpage_round_up_page(sizeof(fpage_region_header_t)) / FPAGE_PAGE_SIZE, 0);

		header->prev = NULL;
		header->next = kernel_address_space.regions_head;
		if (header->next) {
			space_map_temporarily_auto_type(&kernel_address_space, header->next)->prev = &header->next;
		}
		header->page_count = virt_page_count;
		header->start = usable_start = (void*)(virt_start + FPAGE_PAGE_SIZE + (extra_bitmap_page_count * FPAGE_PAGE_SIZE));

		flock_spin_intsafe_init(&header->lock);

		kernel_address_space.regions_head = header;

		// clear out the bitmap
		simple_memset(&header->bitmap[0], 0, HEADER_BITMAP_SPACE);
		for (size_t i = 0; i < extra_bitmap_page_count; ++i) {
			uint8_t* phys_page = allocate_frame(1, 0, NULL);
			uint8_t* page = (uint8_t*)(virt_start + FPAGE_PAGE_SIZE + (i * FPAGE_PAGE_SIZE));

			if (!phys_page) {
				// ack. we've gotta undo all the work we've done up 'till now.
				for (; i > 0; --i) {
					uint8_t* page = (uint8_t*)(virt_start + FPAGE_PAGE_SIZE + ((i - 1) * FPAGE_PAGE_SIZE));
					uint8_t* phys_page = (void*)fpage_space_virtual_to_physical(&kernel_address_space, (uintptr_t)page);
					free_frame(phys_page, 1);
					break_entry(4, FPAGE_VIRT_L4(page), FPAGE_VIRT_L3(page), FPAGE_VIRT_L2(page), FPAGE_VIRT_L1(page));
				}

				failed_to_allocate_bitmap = true;
				break;
			}

			space_map_frame_fixed(&kernel_address_space, phys_page, page, 1, 0);

			simple_memset(page, 0, FPAGE_PAGE_SIZE);
		}

		if (failed_to_allocate_bitmap) {
			free_frame((void*)fpage_virtual_to_physical((uintptr_t)header), fpage_round_up_page(sizeof(fpage_region_header_t)) / FPAGE_PAGE_SIZE);
			break_entry(4, FPAGE_VIRT_L4(header), FPAGE_VIRT_L3(header), FPAGE_VIRT_L2(header), FPAGE_VIRT_L1(header));
			virt_start += (1 + virt_page_count + extra_bitmap_page_count) * FPAGE_PAGE_SIZE;
			continue;
		}

		// clear out the buckets
		simple_memset(&header->buckets[0], 0, sizeof(header->buckets));

		while (pages_allocated < virt_page_count) {
			size_t order = max_order_of_page_count(virt_page_count - pages_allocated);
			size_t pages = page_count_of_order(order);
			void* addr = (void*)((uintptr_t)usable_start + (pages_allocated * FPAGE_PAGE_SIZE));

			space_insert_virtual_free_block(&kernel_address_space, header, addr, pages);

			pages_allocated += pages;
		}

		// this will overflow to 0 for the last region in the virtual address space
		virt_start += (1 + extra_bitmap_page_count + virt_page_count) * FPAGE_PAGE_SIZE;

		total_virt_page_count += virt_page_count;

		// we've reached the max amount of virtual memory. stop here.
		if (total_virt_page_count >= max_virt_page_count) {
			break;
		}
	}

	// register our page fault handler
	fpanic_status(fint_register_special_handler(fint_special_interrupt_page_fault, page_fault_handler, NULL));
};

// NOTE: the table used with the first call to this function is not freed by it, no matter if `also_free` is used
// also, `fpage_flush_table_internal` is a terrible name for this, because if @p needs_flush is `false`, nothing will actually be flushed from the TLB
static void fpage_flush_table_internal(fpage_table_t* phys_table, size_t level_count, uint16_t l4, uint16_t l3, uint16_t l2, bool needs_flush, bool flush_recursive_too, bool also_break, bool also_free) {
	fpage_table_t* virt_table;

	for (size_t i = 0; i < TABLE_ENTRY_COUNT; ++i) {
		virt_table = map_temporarily_auto(phys_table);
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
						fpage_invalidate_tlb_for_address((void*)fpage_make_virtual_address(l4, i, 0, 0, 0));
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
						fpage_invalidate_tlb_for_address((void*)fpage_make_virtual_address(l4, l3, i, 0, 0));
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
					fpage_invalidate_tlb_for_address((void*)fpage_make_virtual_address(l4, l3, l2, i, 0));
				}
			} break;
		}

		if (also_free) {
			free_frame((void*)fpage_entry_address(entry), page_count);
		}
	}

	if (flush_recursive_too) {
		fpage_invalidate_tlb_for_address((void*)fpage_virtual_address_for_table(level_count, l4, l3, l2));
	}
};

#if 0
/**
 * Flushes the TLB for a range of virtual addresses.
 *
 * On return, the TLB should not contain any entries at all from the given table nor any of its subtables.
 *
 * @param phys_table          The **physical** address of the table to flush.
 * @param level_count         How many levels deep the table is located at.
 *                            0 means we're flushing the root page table.
 *                            1 means we're flushing an L3 page table and @p l4 is used.
 *                            2 means we're flushing an L2 page table and @p l4 and @p l3 are used.
 *                            3 means we're flushing an L1 page table and @p l4, @p l3, and @p l2 are used.
 * @param l4                  The L4 index at which the table can be found (if applicable).
 * @param l3                  The L3 index at which the table can be found (if applicable).
 * @param l2                  The L2 index at which the table can be found (if applicable).
 * @param flush_recursive_too Whether to flush the TLB entries for the recursive entry pointing to the given table and its subtables as well.
 *
 * @note This is different from fpage_invalidate_tlb_for_range().
 *       This function only flushes addresses with valid entries, while fpage_invalidate_tlb_for_range()
 *       flushes ALL addresses in the range, which may be *significantly* less efficient than this method.
 */
static void fpage_flush_table(fpage_table_t* phys_table, size_t level_count, uint16_t l4, uint16_t l3, uint16_t l2, bool flush_recursive_too) {
	return fpage_flush_table_internal(phys_table, level_count, l4, l3, l2, true, flush_recursive_too, false, false);
};
#endif

static void fpage_space_flush_mapping_internal(fpage_space_t* space, void* address, size_t page_count, bool needs_flush, bool also_break, bool also_free) {
	while (page_count > 0) {
		uint16_t l4 = FPAGE_VIRT_L4(address);
		uint16_t l3 = FPAGE_VIRT_L3(address);
		uint16_t l2 = FPAGE_VIRT_L2(address);
		uint16_t l1 = FPAGE_VIRT_L1(address);

		fpage_table_t* table = NULL;
		uint64_t entry = 0;

		if (space) {
			table = map_temporarily_auto(space->l4_table);
		} else {
			table = (void*)fpage_virtual_address_for_table(0, 0, 0, 0);
		}

		entry = table->entries[l4];

		// check if L4 is active
		if (!fpage_entry_is_active(entry)) {
			page_count -= (page_count < FPAGE_SUPER_LARGE_PAGE_COUNT) ? page_count : FPAGE_SUPER_LARGE_PAGE_COUNT;
			address = (void*)((uintptr_t)address + FPAGE_SUPER_LARGE_PAGE_SIZE);
			continue;
		}

		// at L4, large pages are not allowed, so no need to check

		table = map_temporarily_auto((void*)fpage_entry_address(entry));
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
				fpage_invalidate_tlb_for_address((void*)fpage_make_virtual_address(l4, l3, 0, 0, 0));
			}

			if (also_free) {
				free_frame((void*)fpage_entry_address(entry), FPAGE_VERY_LARGE_PAGE_COUNT);
			}

			page_count -= FPAGE_VERY_LARGE_PAGE_COUNT;
			address = (void*)((uintptr_t)address + FPAGE_VERY_LARGE_PAGE_SIZE);
			continue;
		}

		table = map_temporarily_auto((void*)fpage_entry_address(entry));
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
				fpage_invalidate_tlb_for_address((void*)fpage_make_virtual_address(l4, l3, l2, 0, 0));
			}

			if (also_free) {
				free_frame((void*)fpage_entry_address(entry), FPAGE_LARGE_PAGE_COUNT);
			}

			page_count -= FPAGE_LARGE_PAGE_COUNT;
			address = (void*)((uintptr_t)address + FPAGE_LARGE_PAGE_SIZE);
			continue;
		}

		table = map_temporarily_auto((void*)fpage_entry_address(entry));
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
			fpage_invalidate_tlb_for_address((void*)fpage_make_virtual_address(l4, l3, l2, l1, 0));
		}

		if (also_free) {
			free_frame((void*)fpage_entry_address(entry), 1);
		}

		--page_count;
		address = (void*)((uintptr_t)address + FPAGE_PAGE_SIZE);
	}
};

/**
 * Similar to fpage_invalidate_tlb_for_address(), but will only flush present entries.
 * Thus, it can only be used in cases where it is known that absent entries are not in the TLB.
 * However, it is *far* more efficient than fpage_invalidate_tlb_for_address() in these cases.
 */
static void fpage_flush_mapping(void* address, size_t page_count) {
	fpage_space_flush_mapping_internal(NULL, address, page_count, true, false, false);
};

/**
 * Similar to fpage_flush_mapping(), but also breaks the lowest entries in the table (i.e. marks them as absent)
 * as they're being flushed so that they become invalid and generate page faults upon translation/usage.
 */
static void fpage_break_mapping(void* address, size_t page_count) {
	fpage_space_flush_mapping_internal(NULL, address, page_count, true, true, false);
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

	frame = allocate_frame(page_count, alignment_power, &allocated);
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

	free_frame(physical_address, page_count);

out:
	return status;
};

ferr_t fpage_map_kernel_any(void* physical_address, size_t page_count, void** out_virtual_address, fpage_flags_t flags) {
	return fpage_space_map_any(&kernel_address_space, physical_address, page_count, out_virtual_address, flags);
};

ferr_t fpage_unmap_kernel(void* virtual_address, size_t page_count) {
	return fpage_space_unmap(&kernel_address_space, virtual_address, page_count);
};

ferr_t fpage_allocate_kernel(size_t page_count, void** out_virtual_address, fpage_flags_t flags) {
	return fpage_space_allocate(&kernel_address_space, page_count, out_virtual_address, flags);
};

ferr_t fpage_free_kernel(void* virtual_address, size_t page_count) {
	return fpage_space_free(&kernel_address_space, virtual_address, page_count);
};

FERRO_WUR ferr_t fpage_space_init(fpage_space_t* space) {
	space->l4_table = allocate_frame(1, 0, NULL);

	if (!space->l4_table) {
		return ferr_temporary_outage;
	}

	fpage_table_t* table = map_temporarily_auto(space->l4_table);

	simple_memset(table, 0, sizeof(fpage_table_t));

	// initialize the buddy allocator's region

	uintptr_t virt_start = fpage_make_virtual_address(FPAGE_USER_L4_MAX, 0, 0, 0, 0);
	size_t virt_page_count = MAX_VIRTUAL_KERNEL_BUDDY_ALLOCATOR_PAGE_COUNT_COEFFICIENT * total_phys_page_count;
	size_t bitmap_byte_count = 0;
	size_t extra_bitmap_page_count = 0;

	// we need at least one page for the header
	--virt_page_count;

	// we might need more for the bitmap
	// divide by 8 because each page is represented by a bit
	bitmap_byte_count = (virt_page_count + 7) / 8;

	// figure out if we need more space for the bitmap than what's left over from the header
	if (bitmap_byte_count >= HEADER_BITMAP_SPACE) {
		// extra pages are required for the bitmap
		extra_bitmap_page_count = fpage_round_up_page(bitmap_byte_count - HEADER_BITMAP_SPACE) / FPAGE_PAGE_SIZE;

		// not enough pages? welp.
		fassert(extra_bitmap_page_count < virt_page_count);

		virt_page_count -= extra_bitmap_page_count;
	}

	// okay, we're definitely going to use this region

	fpage_region_header_t* phys_header = allocate_frame(fpage_round_up_page(sizeof(fpage_region_header_t)) / FPAGE_PAGE_SIZE, 0, NULL);

	if (!phys_header) {
		free_frame(space->l4_table, 1);
		return ferr_temporary_outage;
	}

	fpage_region_header_t* space_header = (void*)virt_start;
	space_map_frame_fixed(space, phys_header, space_header, 1, 0);

	fpage_region_header_t* temp_header = map_temporarily_auto(phys_header);

	void* usable_start = NULL;

	temp_header->prev = NULL;
	temp_header->next = NULL;
	temp_header->page_count = virt_page_count;
	temp_header->start = usable_start = (void*)(virt_start + FPAGE_PAGE_SIZE + (extra_bitmap_page_count * FPAGE_PAGE_SIZE));

	flock_spin_intsafe_init(&temp_header->lock);

	bool failed_to_allocate_bitmap = false;

	// clear out the bitmap
	simple_memset(&temp_header->bitmap[0], 0, HEADER_BITMAP_SPACE);
	for (size_t i = 0; i < extra_bitmap_page_count; ++i) {
		uint8_t* phys_page = allocate_frame(1, 0, NULL);
		uint8_t* page = (uint8_t*)(virt_start + FPAGE_PAGE_SIZE + (i * FPAGE_PAGE_SIZE));

		if (!phys_page) {
			// ack. we've gotta undo all the work we've done up 'till now.
			for (; i > 0; --i) {
				uint8_t* page = (uint8_t*)(virt_start + FPAGE_PAGE_SIZE + ((i - 1) * FPAGE_PAGE_SIZE));
				uint8_t* phys_page = (void*)fpage_space_virtual_to_physical(space, (uintptr_t)page);
				free_frame(phys_page, 1);
			}

			failed_to_allocate_bitmap = true;
			break;
		}

		space_map_frame_fixed(space, phys_page, page, 1, 0);

		page = map_temporarily_auto(phys_page);
		simple_memset(page, 0, FPAGE_PAGE_SIZE);
	}

	if (failed_to_allocate_bitmap) {
		free_frame((void*)phys_header, fpage_round_up_page(sizeof(fpage_region_header_t)) / FPAGE_PAGE_SIZE);
		fpage_flush_table_internal(space->l4_table, 0, 0, 0, 0, false, false, false, true);
		free_frame(space->l4_table, 1);
		return ferr_temporary_outage;
	}

	temp_header = map_temporarily_auto(phys_header);

	// clear out the buckets
	simple_memset(&temp_header->buckets[0], 0, sizeof(temp_header->buckets));

	size_t pages_allocated = 0;
	while (pages_allocated < virt_page_count) {
		size_t order = max_order_of_page_count(virt_page_count - pages_allocated);
		size_t pages = page_count_of_order(order);
		void* addr = (void*)((uintptr_t)usable_start + (pages_allocated * FPAGE_PAGE_SIZE));

		space_insert_virtual_free_block(space, space_header, addr, pages);

		pages_allocated += pages;
	}

	space->regions_head = space_header;
	space->active = false;
	space->mappings = NULL;

	flock_spin_intsafe_init(&space->regions_head_lock);
	flock_spin_intsafe_init(&space->allocation_lock);
	flock_spin_intsafe_init(&space->mappings_lock);

	fwaitq_init(&space->space_destruction_waiters);

	return ferr_ok;
};

void fpage_space_destroy(fpage_space_t* space) {
	fpage_space_mapping_t* next = NULL;

	fwaitq_wake_many(&space->space_destruction_waiters, SIZE_MAX);

	fint_disable();

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

		FERRO_WUR_IGNORE(fmempool_free(curr));
	}

	fpage_flush_table_internal(space->l4_table, 0, 0, 0, 0, space->active, space->active, true, true);

	// the buddy allocator's region header is placed within the address space,
	// so the above call should've already taken care of freeing it (including all of its blocks).

	space->regions_head = NULL;

	free_frame(space->l4_table, 1);
	space->l4_table = NULL;

	// FIXME: we need to check all the CPU cores and see if any one of them is using this address space
	fpage_space_t** current_address_space = fpage_space_current_pointer();
	if (*current_address_space == space) {
		*current_address_space = &kernel_address_space;
	}

	fint_enable();
};

FERRO_WUR ferr_t fpage_space_swap(fpage_space_t* space) {
	fpage_table_t* l4_table = (void*)fpage_virtual_address_for_table(0, 0, 0, 0);

	if (!space) {
		space = &kernel_address_space;
	}

	fint_disable();

	fpage_space_t** current_address_space = fpage_space_current_pointer();

	if (*current_address_space == space) {
		goto out_locked;
	}

	// we never unload the kernel address space
	if (*current_address_space && *current_address_space != fpage_space_kernel()) {
		fpage_table_t* temp_table = map_temporarily_auto((*current_address_space)->l4_table);
		for (size_t i = 0; i < TABLE_ENTRY_COUNT; ++i) {
			uint64_t entry = temp_table->entries[i];

			if (!fpage_entry_is_active(entry)) {
				continue;
			}

			l4_table->entries[i] = 0;
		}

		(*current_address_space)->active = false;

		// FIXME: the precise table flush (fpage_flush_table()) isn't working, so we're doing a full table flush as a workaround for now.
		//        on x86_64, we could mitigate the performance impact by making kernel addresses "global" entries in the page tables.
		fpage_invalidate_tlb_for_active_space();
	}

	*current_address_space = space;

	if ((*current_address_space)) {
		fpage_table_t* temp_table = map_temporarily_auto((*current_address_space)->l4_table);
		for (size_t i = 0; i < TABLE_ENTRY_COUNT; ++i) {
			uint64_t entry = temp_table->entries[i];

			if (!fpage_entry_is_active(entry)) {
				continue;
			}

			l4_table->entries[i] = entry;
		}

		(*current_address_space)->active = true;
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
	return &kernel_address_space;
};

ferr_t fpage_space_map_aligned(fpage_space_t* space, void* physical_address, size_t page_count, uint8_t alignment_power, void** out_virtual_address, fpage_flags_t flags) {
	void* virt = NULL;

	if (physical_address == NULL || page_count == 0 || page_count == SIZE_MAX || out_virtual_address == NULL) {
		return ferr_invalid_argument;
	}

	virt = space_allocate_virtual(space, page_count, alignment_power, NULL, false);

	if (!virt) {
		return ferr_temporary_outage;
	}

	space_map_frame_fixed(space, physical_address, virt, page_count, flags);

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

	fpage_space_flush_mapping_internal(space, virtual_address, page_count, space->active, true, false);

	space_free_virtual(space, virtual_address, page_count, false);

	return ferr_ok;
};

ferr_t fpage_space_allocate_aligned(fpage_space_t* space, size_t page_count, uint8_t alignment_power, void** out_virtual_address, fpage_flags_t flags) {
	void* virt = NULL;
	ferr_t status = ferr_ok;
	fpage_space_mapping_t* space_mapping = NULL;

#if FPAGE_DEBUG_ALWAYS_PREBIND
	flags |= fpage_flag_prebound;
#endif

	if (page_count == 0 || page_count == SIZE_MAX || out_virtual_address == NULL) {
		return ferr_invalid_argument;
	}

	if ((flags & fpage_flag_prebound) == 0) {
		status = fmempool_allocate_advanced(sizeof(*space_mapping), 0, UINT8_MAX, fmempool_flag_prebound, NULL, (void*)&space_mapping);
		if (status != ferr_ok) {
			goto out;
		}
	}

	// NOTE: allocating fixed addresses within the buddy allocator's region(s) is not allowed,
	//       so there is no need to acquire the allocation lock here.
	//       the buddy allocator already has its own locks.

	virt = space_allocate_virtual(space, page_count, alignment_power, NULL, false);

	if (!virt) {
		return ferr_temporary_outage;
	}

	if (flags & fpage_flag_prebound) {
		for (size_t i = 0; i < page_count; ++i) {
			void* frame = allocate_frame(1, 0, NULL);

			if (!frame) {
				for (; i > 0; --i) {
					uintptr_t virt_frame = (uintptr_t)virt + ((i - 1) * FPAGE_PAGE_SIZE);
					free_frame((void*)fpage_space_virtual_to_physical(space, virt_frame), 1);
					fpage_space_flush_mapping_internal(space, (void*)virt_frame, 1, space->active, true, false);
				}
				space_free_virtual(space, virt, page_count, false);
				return ferr_temporary_outage;
			}

			space_map_frame_fixed(space, frame, (void*)((uintptr_t)virt + (i * FPAGE_PAGE_SIZE)), 1, flags);
		}

		if (flags & fpage_flag_zero) {
			// zero out the memory now, since we're prebinding
			simple_memset(virt, 0, page_count * FPAGE_PAGE_SIZE);
		}
	} else {
		space_map_frame_fixed(space, (void*)ON_DEMAND_MAGIC, virt, page_count, flags | fpage_private_flag_inactive | fpage_private_flag_repeat);

		flock_spin_intsafe_lock(&space->mappings_lock);

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

		flock_spin_intsafe_unlock(&space->mappings_lock);
	}

out:
	if (status == ferr_ok) {
		*out_virtual_address = virt;
	} else {
		if (virt) {
			space_free_virtual(space, virt, page_count, false);
		}

		if (space_mapping) {
			FERRO_WUR_IGNORE(fmempool_free(space_mapping));
		}
	}
	return status;
};

ferr_t fpage_space_allocate(fpage_space_t* space, size_t page_count, void** out_virtual_address, fpage_flags_t flags) {
	return fpage_space_allocate_aligned(space, page_count, 0, out_virtual_address, flags);
};

// MUST be holding the allocation lock
static bool space_region_is_free(fpage_space_t* space, uintptr_t virtual_address, size_t page_count) {
	while (page_count > 0) {
		uint16_t l4 = FPAGE_VIRT_L4(virtual_address);
		uint16_t l3 = FPAGE_VIRT_L3(virtual_address);
		uint16_t l2 = FPAGE_VIRT_L2(virtual_address);
		uint16_t l1 = FPAGE_VIRT_L1(virtual_address);
		uint16_t offset = FPAGE_VIRT_OFFSET(virtual_address);

		fpage_table_t* table = map_temporarily_auto(space->l4_table);
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

		table = map_temporarily_auto((void*)fpage_entry_address(entry));
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

		table = map_temporarily_auto((void*)fpage_entry_address(entry));
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

		table = map_temporarily_auto((void*)fpage_entry_address(entry));
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

ferr_t fpage_space_allocate_fixed(fpage_space_t* space, size_t page_count, void* virtual_address, fpage_flags_t flags) {
	ferr_t status = ferr_ok;
	fpage_space_mapping_t* space_mapping = NULL;

#if FPAGE_DEBUG_ALWAYS_PREBIND
	flags |= fpage_flag_prebound;
#endif

	// if it's in the buddy allocator's region(s), it's reserved for the buddy allocator and can't be mapped for anyone else
	// TODO: allow this to be mapped by allocating it with the buddy allocator
	if (space_region_belongs_to_buddy_allocator(space, virtual_address, page_count)) {
		status = ferr_temporary_outage;
		goto out_unlocked;
	}

	if ((flags & fpage_flag_prebound) == 0) {
		status = fmempool_allocate_advanced(sizeof(*space_mapping), 0, UINT8_MAX, fmempool_flag_prebound, NULL, (void*)&space_mapping);
		if (status != ferr_ok) {
			goto out_unlocked;
		}
	}

	flock_spin_intsafe_lock(&space->allocation_lock);

	if (!space_region_is_free(space, (uintptr_t)virtual_address, page_count)) {
		status = ferr_temporary_outage;
		goto out_locked;
	}

	if (flags & fpage_flag_prebound) {
		for (size_t i = 0; i < page_count; ++i) {
			void* frame = allocate_frame(1, 0, NULL);

			if (!frame) {
				for (; i > 0; --i) {
					uintptr_t virt_frame = (uintptr_t)virtual_address + ((i - 1) * FPAGE_PAGE_SIZE);
					free_frame((void*)fpage_space_virtual_to_physical(space, virt_frame), 1);
					fpage_space_flush_mapping_internal(space, (void*)virt_frame, 1, space->active, true, false);
				}
				status = ferr_temporary_outage;
				goto out_locked;
			}

			space_map_frame_fixed(space, frame, (void*)((uintptr_t)virtual_address + (i * FPAGE_PAGE_SIZE)), 1, flags);
		}

		if (flags & fpage_flag_zero) {
			// zero out the memory now, since we're prebinding
			simple_memset(virtual_address, 0, page_count * FPAGE_PAGE_SIZE);
		}
	} else {
		space_map_frame_fixed(space, (void*)ON_DEMAND_MAGIC, virtual_address, page_count, flags | fpage_private_flag_inactive | fpage_private_flag_repeat);

		flock_spin_intsafe_lock(&space->mappings_lock);

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

		flock_spin_intsafe_unlock(&space->mappings_lock);
	}

out_locked:
	flock_spin_intsafe_unlock(&space->allocation_lock);
out_unlocked:
	if (status != ferr_ok) {
		if (space_mapping) {
			FERRO_WUR_IGNORE(fmempool_free(space_mapping));
		}
	}
	return status;
};

ferr_t fpage_space_free(fpage_space_t* space, void* virtual_address, size_t page_count) {
	fpage_space_mapping_t* mapping = NULL;

	if (virtual_address == NULL || page_count == 0 || page_count == SIZE_MAX) {
		return ferr_invalid_argument;
	}

	flock_spin_intsafe_lock(&space->mappings_lock);
	for (mapping = space->mappings; mapping != NULL; mapping = mapping->next) {
		if (
			mapping->virtual_address <= (uintptr_t)virtual_address &&
			mapping->virtual_address + (mapping->page_count * FPAGE_PAGE_SIZE) >= (uintptr_t)virtual_address + (page_count * FPAGE_PAGE_SIZE)
		) {
			// this is the mapping that contains the target address

			// TODO: maybe add support for freeing only part of an allocation?

			if (mapping->virtual_address != (uintptr_t)virtual_address || mapping->page_count != page_count) {
				flock_spin_intsafe_unlock(&space->mappings_lock);
				return ferr_invalid_argument;
			}

			if (mapping->mapping) {
				// shareable mappings can only be removed via fpage_space_remove_mapping()
				flock_spin_intsafe_unlock(&space->mappings_lock);
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
	flock_spin_intsafe_unlock(&space->mappings_lock);

	// it's cheaper to just acquire the allocation lock in all cases rather than check if the region belongs to the buddy allocator
	// TODO: check if it's actually cheaper
	flock_spin_intsafe_lock(&space->allocation_lock);

	// this will take care of freeing the frames for this mapping;
	// this will also handle the case of having bound-on-demand pages within the mapping (it'll just zero those out).
	fpage_space_flush_mapping_internal(space, virtual_address, page_count, space->active, true, true);

	// ask the buddy allocator to free this in all cases.
	// it'll check if the region is actually part of the buddy allocator's region(s)
	// if so, it'll free it. otherwise, it'll just return.
	space_free_virtual(space, virtual_address, page_count, false);

	flock_spin_intsafe_unlock(&space->allocation_lock);

	if (mapping) {
		FERRO_WUR_IGNORE(fmempool_free(mapping));
	}

	return ferr_ok;
};

ferr_t fpage_space_map_fixed(fpage_space_t* space, void* physical_address, size_t page_count, void* virtual_address, fpage_flags_t flags) {
	if (physical_address == NULL || page_count == 0 || page_count == SIZE_MAX || virtual_address == NULL) {
		return ferr_invalid_argument;
	}

	// TODO: make sure we don't have a mapping there already

	space_map_frame_fixed(space, physical_address, virtual_address, page_count, flags);

	return ferr_ok;
};

ferr_t fpage_space_reserve_any(fpage_space_t* space, size_t page_count, void** out_virtual_address) {
	void* virt = NULL;

	if (page_count == 0 || page_count == SIZE_MAX || !out_virtual_address) {
		return ferr_invalid_argument;
	}

	virt = space_allocate_virtual(space, page_count, 0, NULL, false);

	if (!virt) {
		return ferr_temporary_outage;
	}

	*out_virtual_address = virt;

	return ferr_ok;
};

ferr_t fpage_space_insert_mapping(fpage_space_t* space, fpage_mapping_t* mapping, size_t page_offset, size_t page_count, uint8_t alignment_power, fpage_flags_t flags, void** out_virtual_address) {
	ferr_t status = ferr_ok;
	fpage_space_mapping_t* space_mapping = NULL;
	void* alloc_addr = NULL;

	status = fpage_mapping_retain(mapping);
	if (status != ferr_ok) {
		mapping = NULL;
		goto out;
	}

	status = fmempool_allocate_advanced(sizeof(*space_mapping), 0, UINT8_MAX, fmempool_flag_prebound, NULL, (void*)&space_mapping);
	if (status != ferr_ok) {
		goto out;
	}

	alloc_addr = space_allocate_virtual(space, page_count, alignment_power, NULL, false);
	if (!alloc_addr) {
		status = ferr_temporary_outage;
		goto out;
	}

	space_mapping->mapping = mapping;
	space_mapping->virtual_address = (uintptr_t)alloc_addr;
	space_mapping->page_count = page_count;
	space_mapping->page_offset = page_offset;
	space_mapping->flags = flags;

	flock_spin_intsafe_lock(&space->mappings_lock);
	space_mapping->prev = &space->mappings;
	space_mapping->next = *space_mapping->prev;

	if (space_mapping->next) {
		space_mapping->next->prev = &space_mapping->next;
	}
	*space_mapping->prev = space_mapping;
	flock_spin_intsafe_unlock(&space->mappings_lock);

	// TODO: eagerly map the portions that are already bound.
	//       this method (mapping them as on-demand) does work (it'll fault on each portion and map in the already-bound portion from the mapping),
	//       but it's not terribly efficient.
	space_map_frame_fixed(space, (void*)ON_DEMAND_MAGIC, alloc_addr, page_count, flags | fpage_private_flag_inactive | fpage_private_flag_repeat);

out:
	if (status == ferr_ok) {
		*out_virtual_address = alloc_addr;
	} else {
		if (alloc_addr) {
			space_free_virtual(space, alloc_addr, page_count, false);
		}
		if (space_mapping) {
			FERRO_WUR_IGNORE(fmempool_free(space_mapping));
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

	flock_spin_intsafe_lock(&space->mappings_lock);
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
	flock_spin_intsafe_unlock(&space->mappings_lock);

out:
	return status;
};

ferr_t fpage_space_remove_mapping(fpage_space_t* space, void* virtual_address) {
	ferr_t status = ferr_ok;
	fpage_space_mapping_t* space_mapping = NULL;

	flock_spin_intsafe_lock(&space->mappings_lock);
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
	flock_spin_intsafe_unlock(&space->mappings_lock);

	if (status != ferr_ok) {
		goto out;
	}

	// now break the mapping in the page tables
	fpage_space_flush_mapping_internal(space, (void*)space_mapping->virtual_address, space_mapping->page_count, space->active, true, false);

	// and free the allocated virtual region
	space_free_virtual(space, (void*)space_mapping->virtual_address, space_mapping->page_count, false);

	// finally, release the mapping and discard the space mapping
	fpage_mapping_release(space_mapping->mapping);
	FERRO_WUR_IGNORE(fmempool_free(space_mapping));

out:
	return status;
};

// splits one or more existing mappings that contain the given region so that
// the region will be in one or more mapping structures that start and end with the region.
static ferr_t fpage_space_split_mapping(fpage_space_t* space, void* region_start, size_t region_page_count, bool locked) {
	ferr_t status = ferr_ok;
	size_t found = 0;
	size_t required_mapping_structs = 0;
	fpage_space_mapping_t* allocated = NULL;

	if (!locked) {
		flock_spin_intsafe_lock(&space->mappings_lock);
	}

	// first, check if we do indeed have the entire region in a set of mappings
	// (we fail if part of the region is not in some mapping)
	// we also determine how many additional space mapping structures we need to create

	for (fpage_space_mapping_t* space_mapping = space->mappings; space_mapping != NULL; space_mapping = space_mapping->next) {
		if (
			space_mapping->virtual_address == (uintptr_t)region_start &&
			space_mapping->virtual_address + (FPAGE_PAGE_SIZE * space_mapping->page_count) == (uintptr_t)region_start + (FPAGE_PAGE_SIZE * region_page_count)
		) {
			// this mapping is the entire region
			found = region_page_count;
			required_mapping_structs = 0;
			break;
		}

		if (
			space_mapping->virtual_address < (uintptr_t)region_start &&
			space_mapping->virtual_address + (FPAGE_PAGE_SIZE * space_mapping->page_count) > (uintptr_t)region_start + (FPAGE_PAGE_SIZE * region_page_count)
		) {
			// this mapping contains the entire region
			found = region_page_count;
			required_mapping_structs = 2;
			break;
		}

		if (
			space_mapping->virtual_address > (uintptr_t)region_start &&
			space_mapping->virtual_address + (FPAGE_PAGE_SIZE * space_mapping->page_count) < (uintptr_t)region_start + (FPAGE_PAGE_SIZE * region_page_count)
		) {
			// this mapping is contained entirely within the region
			found += space_mapping->page_count;
			continue;
		}

		if (
			space_mapping->virtual_address < (uintptr_t)region_start &&
			space_mapping->virtual_address + (FPAGE_PAGE_SIZE * space_mapping->page_count) <= (uintptr_t)region_start + (FPAGE_PAGE_SIZE * region_page_count)
		) {
			// this mapping contains the start of the region
			found += space_mapping->page_count;
			++required_mapping_structs;
			continue;
		}

		if (
			space_mapping->virtual_address > (uintptr_t)region_start &&
			space_mapping->virtual_address + (FPAGE_PAGE_SIZE * space_mapping->page_count) >= (uintptr_t)region_start + (FPAGE_PAGE_SIZE * region_page_count)
		) {
			// this mapping contains the end of the region
			found += space_mapping->page_count;
			++required_mapping_structs;
			continue;
		}
	}

	if (found != region_page_count) {
		status = ferr_invalid_argument;
		goto out;
	}

	if (required_mapping_structs == 0) {
		// we actually don't need to split anything up
		// go ahead and exit
		status = ferr_ok;
		goto out;
	}

	// now allocate the additional mappings we need
	for (size_t i = 0; i < required_mapping_structs; ++i) {
		fpage_space_mapping_t* tmp = NULL;

		status = fmempool_allocate_advanced(sizeof(*tmp), 0, UINT8_MAX, fmempool_flag_prebound, NULL, (void*)&tmp);
		if (status != ferr_ok) {
			goto out;
		}

		tmp->prev = &allocated;
		tmp->next = allocated;

		*tmp->prev = tmp;
		if (tmp->next) {
			tmp->next->prev = &tmp->next;
		}

		tmp->mapping = NULL;
		tmp->virtual_address = 0;
		tmp->page_count = 0;
		tmp->page_offset = 0;
		tmp->flags = 0;
		tmp->permissions = 0;
	}

	// now populate the additional mappings
	for (fpage_space_mapping_t* space_mapping = space->mappings; space_mapping != NULL; space_mapping = space_mapping->next) {
		if (
			space_mapping->virtual_address == (uintptr_t)region_start &&
			space_mapping->virtual_address + (FPAGE_PAGE_SIZE * space_mapping->page_count) == (uintptr_t)region_start + (FPAGE_PAGE_SIZE * region_page_count)
		) {
			// this mapping is the entire region
			// (this is impossible)
			fpanic("Found entire region in mapping, but this should be impossible here");
		}

		if (
			space_mapping->virtual_address < (uintptr_t)region_start &&
			space_mapping->virtual_address + (FPAGE_PAGE_SIZE * space_mapping->page_count) > (uintptr_t)region_start + (FPAGE_PAGE_SIZE * region_page_count)
		) {
			// this mapping contains the entire region

			// split it into three parts
			fpage_space_mapping_t* start = NULL;
			fpage_space_mapping_t* end = NULL;
			fpage_space_mapping_t* middle = space_mapping;

			// take the start mapping out of the allocated mapping list
			start = allocated;
			*start->prev = start->next;
			if (start->next) {
				start->next->prev = start->prev;
			}

			// same for the end mapping
			end = allocated;
			*end->prev = end->next;
			if (end->next) {
				end->next->prev = end->prev;
			}

			// now link it into the space's mapping list
			start->prev = &space->mappings;
			start->next = space->mappings;

			*start->prev = start;
			if (start->next) {
				start->next->prev = &start->next;
			}

			// same for the end mapping
			end->prev = &space->mappings;
			end->next = space->mappings;

			*end->prev = end;
			if (end->next) {
				end->next->prev = &end->next;
			}

			// now populate the start mapping
			start->mapping = space_mapping->mapping;
			start->virtual_address = space_mapping->virtual_address;
			start->page_count = fpage_round_up_to_page_count((uintptr_t)region_start - start->virtual_address);
			start->page_offset = space_mapping->page_offset;
			start->flags = space_mapping->flags;
			start->permissions = space_mapping->permissions;

			// and now the end mapping
			end->mapping = space_mapping->mapping;
			end->virtual_address = (uintptr_t)region_start + (FPAGE_PAGE_SIZE * region_page_count);
			end->page_count = fpage_round_up_to_page_count((space_mapping->virtual_address + (FPAGE_PAGE_SIZE * space_mapping->page_count)) - end->virtual_address);
			end->page_offset = space_mapping->page_offset + fpage_round_up_to_page_count(end->virtual_address - start->virtual_address);
			end->flags = space_mapping->flags;
			end->permissions = space_mapping->permissions;

			// now populate the middle mapping
			// (this must be done after start and end since it's the old space_mapping)
			middle->virtual_address = (uintptr_t)region_start;
			middle->page_count = region_page_count;
			middle->page_offset = start->page_offset + fpage_round_up_to_page_count(middle->virtual_address - start->virtual_address);

			// finally, if this space mapping contains a backing mapping, reference it for each of the created space mappings
			if (middle->mapping) {
				fpanic_status(fpage_mapping_retain(middle->mapping));
				fpanic_status(fpage_mapping_retain(middle->mapping));
			}

			break;
		}

		if (
			space_mapping->virtual_address > (uintptr_t)region_start &&
			space_mapping->virtual_address + (FPAGE_PAGE_SIZE * space_mapping->page_count) < (uintptr_t)region_start + (FPAGE_PAGE_SIZE * region_page_count)
		) {
			// this mapping is contained entirely within the region
			// we actually don't need to do anything here
			continue;
		}

		if (
			space_mapping->virtual_address < (uintptr_t)region_start &&
			space_mapping->virtual_address + (FPAGE_PAGE_SIZE * space_mapping->page_count) <= (uintptr_t)region_start + (FPAGE_PAGE_SIZE * region_page_count)
		) {
			// this mapping contains the start of the region

			// let's split it in two

			fpage_space_mapping_t* start = NULL;
			fpage_space_mapping_t* end = space_mapping;

			// take the start mapping out of the allocated mapping list
			start = allocated;
			*start->prev = start->next;
			if (start->next) {
				start->next->prev = start->prev;
			}

			// now link it into the space's mapping list
			start->prev = &space->mappings;
			start->next = space->mappings;

			*start->prev = start;
			if (start->next) {
				start->next->prev = &start->next;
			}

			// now populate the start mapping
			start->mapping = space_mapping->mapping;
			start->virtual_address = space_mapping->virtual_address;
			start->page_count = fpage_round_up_to_page_count((uintptr_t)region_start - start->virtual_address);
			start->page_offset = space_mapping->page_offset;
			start->flags = space_mapping->flags;
			start->permissions = space_mapping->permissions;

			// now populate the end mapping
			// (this must be done after start since it's the old space_mapping)
			end->virtual_address = (uintptr_t)region_start;
			end->page_count = space_mapping->page_count - start->page_count;
			end->page_offset = start->page_offset + fpage_round_up_to_page_count(end->virtual_address - start->virtual_address);

			// finally, if this space mapping contains a backing mapping, reference it for the created space mapping
			if (end->mapping) {
				fpanic_status(fpage_mapping_retain(end->mapping));
			}

			continue;
		}

		if (
			space_mapping->virtual_address > (uintptr_t)region_start &&
			space_mapping->virtual_address + (FPAGE_PAGE_SIZE * space_mapping->page_count) >= (uintptr_t)region_start + (FPAGE_PAGE_SIZE * region_page_count)
		) {
			// this mapping contains the end of the region

			// let's split it in two

			fpage_space_mapping_t* start = space_mapping;
			fpage_space_mapping_t* end = NULL;

			// take the end mapping out of the allocated mapping list
			end = allocated;
			*end->prev = end->next;
			if (end->next) {
				end->next->prev = end->prev;
			}

			// now link it into the space's mapping list
			end->prev = &space->mappings;
			end->next = space->mappings;

			*end->prev = end;
			if (end->next) {
				end->next->prev = &end->next;
			}

			// now populate the end mapping
			end->mapping = space_mapping->mapping;
			end->virtual_address = (uintptr_t)region_start + (FPAGE_PAGE_SIZE * region_page_count);
			end->page_count = fpage_round_up_to_page_count((space_mapping->virtual_address + (FPAGE_PAGE_SIZE * space_mapping->page_count)) - end->virtual_address);
			end->page_offset = space_mapping->page_offset + fpage_round_up_to_page_count(end->virtual_address - space_mapping->virtual_address);
			end->flags = space_mapping->flags;
			end->permissions = space_mapping->permissions;

			// now populate the start mapping
			// (this must be done after end since it's the old space_mapping)
			start->page_count = space_mapping->page_count - end->page_count;

			// finally, if this space mapping contains a backing mapping, reference it for the created space mapping
			if (start->mapping) {
				fpanic_status(fpage_mapping_retain(start->mapping));
			}

			continue;
		}
	}

out:
	if (!locked) {
		flock_spin_intsafe_unlock(&space->mappings_lock);
	}
out_unlocked:
	if (status != ferr_ok) {
		fpage_space_mapping_t* next = NULL;
		for (fpage_space_mapping_t* mapping = allocated; mapping != NULL; mapping = next) {
			next = mapping->next;
			FERRO_WUR_IGNORE(fmempool_free(mapping));
		}
	}
	return status;
};

ferr_t fpage_space_move_into_mapping(fpage_space_t* space, void* address, size_t page_count, size_t page_offset, fpage_mapping_t* mapping) {
	ferr_t status = ferr_ok;
	fpage_space_mapping_t* space_mapping = NULL;

	flock_spin_intsafe_lock(&space->mappings_lock);

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
		status = fmempool_allocate_advanced(sizeof(*space_mapping), 0, UINT8_MAX, fmempool_flag_prebound, NULL, (void*)&space_mapping);
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

		status = fpage_mapping_bind(mapping, page_offset + i, portion_page_count, (void*)phys, 0);
		if (status != ferr_ok) {
			goto out;
		}

		i += portion_page_count - 1;
	}

out:
	flock_spin_intsafe_unlock(&space->mappings_lock);
out_unlocked:
	return status;
};

ferr_t fpage_space_change_permissions(fpage_space_t* space, void* address, size_t page_count, fpage_permissions_t permissions) {
	ferr_t status = ferr_no_such_resource;
	fpage_space_mapping_t* space_mapping = NULL;

	// TODO: allow changing permissions for prebound memory

	flock_spin_intsafe_lock(&space->mappings_lock);

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
	flock_spin_intsafe_unlock(&space->mappings_lock);
out_unlocked:
	return status;
};

static void fpage_mapping_destroy(fpage_mapping_t* mapping) {
	fpage_mapping_portion_t* next = NULL;

	for (fpage_mapping_portion_t* curr = mapping->portions; curr != NULL; curr = next) {
		next = curr->next;

		if (curr->flags & fpage_mapping_portion_flag_allocated) {
			free_frame((void*)curr->physical_address, curr->page_count);
		}

		if (curr->flags & fpage_mapping_portion_flag_backing_mapping) {
			fpage_mapping_release(curr->backing_mapping);
		}

		FERRO_WUR_IGNORE(fmempool_free(curr));
	}

	FERRO_WUR_IGNORE(fmempool_free(mapping));
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

	status = fmempool_allocate_advanced(sizeof(*mapping), 0, UINT8_MAX, fmempool_flag_prebound, NULL, (void*)&mapping);
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
			FERRO_WUR_IGNORE(fmempool_free(mapping));
		}
	}
	return status;
};

// this does NOT check if the given portion is already bound
static ferr_t fpage_mapping_bind_locked(fpage_mapping_t* mapping, size_t page_offset, size_t page_count, void* physical_address, fpage_mapping_t* target_mapping, size_t target_mapping_page_offset, fpage_mapping_bind_flags_t flags) {
	ferr_t status = ferr_ok;
	bool free_addr_on_fail = false;
	fpage_mapping_portion_t* new_portion = NULL;

	status = fmempool_allocate_advanced(sizeof(*new_portion), 0, UINT8_MAX, fmempool_flag_prebound, NULL, (void*)&new_portion);
	if (status != ferr_ok) {
		goto out;
	}

	if (!physical_address) {
		physical_address = allocate_frame(page_count, 0, NULL);
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
			simple_memset(map_temporarily_auto(physical_address), 0, page_count * FPAGE_PAGE_SIZE);
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

	if (free_addr_on_fail) {
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
			free_frame(physical_address, page_count);
		}
		if (new_portion) {
			FERRO_WUR_IGNORE(fmempool_free(new_portion));
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

//
// page faults
//

static bool address_is_bound_on_demand(fpage_space_t* space, void* address) {
	uint16_t l4 = FPAGE_VIRT_L4(address);
	uint16_t l3 = FPAGE_VIRT_L3(address);
	uint16_t l2 = FPAGE_VIRT_L2(address);
	uint16_t l1 = FPAGE_VIRT_L1(address);

	fpage_table_t* table = NULL;
	uint64_t entry = 0;

	if (space) {
		table = map_temporarily_auto(space->l4_table);
	} else {
		table = (void*)fpage_virtual_address_for_table(0, 0, 0, 0);
	}

	entry = table->entries[l4];

	// check if L4 is active
	if (!fpage_entry_is_active(entry)) {
		return false;
	}

	// at L4, large pages are not allowed, so no need to check

	table = map_temporarily_auto((void*)fpage_entry_address(entry));
	entry = table->entries[l3];

	// check if L3 is active
	if (!fpage_entry_is_active(entry)) {
		return fpage_entry_address(entry) == ON_DEMAND_MAGIC;
	}

	// at L3, there might be a very large page instead of a table
	if (fpage_entry_is_large_page_entry(entry)) {
		return false;
	}

	table = map_temporarily_auto((void*)fpage_entry_address(entry));
	entry = table->entries[l2];

	// check if L2 is active
	if (!fpage_entry_is_active(entry)) {
		return fpage_entry_address(entry) == ON_DEMAND_MAGIC;
	}

	// at L2, there might be a large page instead of a table
	if (fpage_entry_is_large_page_entry(entry)) {
		return false;
	}

	table = map_temporarily_auto((void*)fpage_entry_address(entry));
	entry = table->entries[l1];

	// check if L1 is active
	if (!fpage_entry_is_active(entry)) {
		return fpage_entry_address(entry) == ON_DEMAND_MAGIC;
	}

	return false;
};

static bool try_handling_fault_with_space(uintptr_t faulting_address, fpage_space_t* space) {
	uintptr_t faulting_page = fpage_round_down_page(faulting_address);

	if (address_is_bound_on_demand(space, (void*)faulting_address)) {
		// try to bind it now

		fpage_space_mapping_t space_mapping_copy;
		void* phys_addr = NULL;
		uint32_t page_offset;
		bool found = false;

		flock_spin_intsafe_lock(&space->mappings_lock);

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
			flock_spin_intsafe_unlock(&space->mappings_lock);
			return false;
		}

retry_bound:
		page_offset = space_mapping_copy.page_offset + fpage_round_down_to_page_count(faulting_page - space_mapping_copy.virtual_address);

		if (space_mapping_copy.mapping) {
			fpage_mapping_t* target_mapping = space_mapping_copy.mapping;

			flock_spin_intsafe_unlock(&space->mappings_lock);

retry_target_mapping:
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
						goto retry_target_mapping;
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

			flock_spin_intsafe_lock(&space->mappings_lock);

			// we had to drop the mappings lock, so someone might've removed the mapping we had.
			// let's see if we can find it again.

			// go ahead and release the extra reference we acquired;
			// the address space can't release it's reference on it as long as we hold the mappings lock
			fpage_mapping_release(target_mapping);

			found = false;

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

			if (!found) {
				// the address is no longer mapped
				flock_spin_intsafe_unlock(&space->mappings_lock);
				return false;
			}

			// okay, everything's good here; we can go ahead and map the given physical frame into the faulted page now
		} else {
			// this is a non-shared bound-on-demand page;
			// just allocate a frame
			phys_addr = allocate_frame(1, 0, NULL);

			if (!phys_addr) {
				// not enough memory to bind it
				flock_spin_intsafe_unlock(&space->mappings_lock);
				return false;
			}

			if (space_mapping_copy.flags & fpage_flag_zero) {
				// zero out the new page
				simple_memset(map_temporarily_auto(phys_addr), 0, FPAGE_PAGE_SIZE);
			}
		}

		// okay, we've got a valid phys_addr here that we're going to map
		space_map_frame_fixed(space, phys_addr, (void*)faulting_page, 1, space_mapping_copy.flags);

		flock_spin_intsafe_unlock(&space->mappings_lock);

		return true;
	}

	return false;
};

static void page_fault_handler(void* context) {
	uintptr_t faulting_address = fpage_fault_address();
	uintptr_t faulting_page = fpage_round_down_page(faulting_address);
	fpage_space_t* space = fpage_space_current();

#if FPAGE_DEBUG_LOG_FAULTS
	fconsole_logf("Handling fault for %p\n", (void*)faulting_address);
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

void fpage_log_early(void) {
	// we're early, so we're running in a uniprocessor environment;
	// all we have to do is disable interrupts and we don't need to take any locks
	fint_disable();

	for (fpage_region_header_t* region = regions_head; region != NULL; region = map_temporarily_auto_type(region)->next) {
		void* start = map_temporarily_auto_type(region)->start;
		fconsole_logf("Paging: physical region %p-%p\n", start, (char*)start + (map_temporarily_auto_type(region)->page_count * FPAGE_PAGE_SIZE));
	}

	fint_enable();
};
