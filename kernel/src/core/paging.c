/**
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
//
// paging.c
//
// physical and virtual memory allocation
//

#include <ferro/core/paging.h>
#include <ferro/core/locks.h>
#include <ferro/bits.h>
#include <libk/libk.h>

// magic value used to identify pages that need to mapped on-demand
#define ON_DEMAND_MAGIC (0xdeadfeedf00dULL << FPAGE_VIRT_L1_SHIFT)

#define TABLE_ENTRY_COUNT (sizeof(root_table->entries) / sizeof(*root_table->entries))
// coefficient that is multiplied by the amount of physical memory available to determine the maximum amount of virtual memory
// the buddy allocator can use. more virtual memory than this can be used, it's just that it'll use a less efficient method of allocation.
#define MAX_VIRTUAL_KERNEL_BUDDY_ALLOCATOR_PAGE_COUNT_COEFFICIENT 16
#define MAX_ORDER 32

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

// free blocks are doubly-linked list nodes
FERRO_STRUCT(fpage_free_block) {
	fpage_free_block_t** prev;
	fpage_free_block_t* next;
};

FERRO_STRUCT(fpage_region_header) {
	fpage_region_header_t** prev;
	fpage_region_header_t* next;
	size_t page_count;
	void* start;
	fpage_free_block_t* buckets[MAX_ORDER];

	// this lock protects the region and all of its blocks from reads and writes (for the bookkeeping info; not the actual content itself, obviously)
	flock_spin_intsafe_t lock;

	// if a bit is 0, the block corresponding to that bit is free.
	// if a bit is 1, the block corresponding to that bit is in use.
	uint8_t bitmap[];
};

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

// we're never going to get more physical memory, so the regions head is never going to be modified; thus, we don't need a lock
//static flock_spin_intsafe_t regions_head_lock = FLOCK_SPIN_INTSAFE_INIT;

// the L2 index pointing to the temporary table
static uint16_t temporary_table_index = 0;
static fpage_table_t temporary_table FERRO_PAGE_ALIGNED = {0};
static fpage_region_header_t* kernel_virtual_regions_head = NULL;

// at the moment, we never modify the virtual regions head. however, in the future, we may want to, so this is necessary.
static flock_spin_intsafe_t kernel_virtual_regions_head_lock = FLOCK_SPIN_INTSAFE_INIT;

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

FERRO_ALWAYS_INLINE size_t min_order_for_page_count(size_t page_count) {
	if (page_count == 0) {
		return SIZE_MAX;
	} else {
		size_t result = ferro_bits_in_use_u64(page_count) - 1;
		if (result >= MAX_ORDER) {
			return MAX_ORDER - 1;
		}
		return (page_count > page_count_of_order(result)) ? (result + 1) : result;
	}
};

FERRO_ALWAYS_INLINE size_t max_order_of_page_count(size_t page_count) {
	if (page_count == 0) {
		return SIZE_MAX;
	} else {
		size_t result = ferro_bits_in_use_u64(page_count) - 1;
		if (result >= MAX_ORDER) {
			return MAX_ORDER - 1;
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

// `slot` must be from 0-511 (inclusive)
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

// like `map_temporarily`, but automatically chooses the next available slot.
// this *will* automatically wrap around once it reaches the maximum,
// but that should be no issue, as it is unlikely that you'll need to use 512 temporary pages simultaneously
FERRO_ALWAYS_INLINE void* map_temporarily_auto(void* physical_address) {
	static size_t next_slot = 0;
	void* address = map_temporarily(physical_address, next_slot++);
	if (next_slot == TABLE_ENTRY_COUNT) {
		next_slot = 0;
	}
	return address;
};

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
static const uint8_t* bitmap_entry_for_block(const fpage_region_header_t* parent_region, const fpage_free_block_t* block, size_t* out_bit_index) {
	size_t bitmap_index = bitmap_bit_index_for_block(parent_region, block);
	size_t byte_index = byte_index_for_bit(bitmap_index);
	size_t byte_bit_index = byte_bit_index_for_bit(bitmap_index);
	const uint8_t* byte = NULL;

	parent_region = map_temporarily_auto((void*)parent_region);

	if (byte_index < HEADER_BITMAP_SPACE) {
		// simple case; no additional mappings required
		byte = &parent_region->bitmap[byte_index];
	} else {
		// complex case; we need to map the page of the bitmap that contains the target byte
		byte = map_temporarily_auto((void*)fpage_round_down_page((uintptr_t)&parent_region->bitmap[byte_index]));
		byte = &byte[(byte_index - HEADER_BITMAP_SPACE) % FPAGE_PAGE_SIZE];
	}

	*out_bit_index = byte_bit_index;

	return byte;
};

/**
 * Returns `true` if the given block is in-use.
 *
 * The parent region's lock MUST be held.
 */
static bool block_is_in_use(const fpage_region_header_t* parent_region, const fpage_free_block_t* block) {
	size_t byte_bit_index = 0;
	const uint8_t* byte = bitmap_entry_for_block(parent_region, block, &byte_bit_index);

	return (*byte) & (1 << byte_bit_index);
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
static void insert_free_block(fpage_region_header_t* parent_region, fpage_free_block_t* block, size_t block_page_count) {
	fpage_free_block_t* phys_block = block;
	size_t order = max_order_of_page_count(block_page_count);
	fpage_region_header_t* phys_parent = parent_region;

	parent_region = map_temporarily_auto(parent_region);
	block = map_temporarily_auto(block);

	block->prev = &phys_parent->buckets[order];
	block->next = parent_region->buckets[order];

	if (block->next) {
		fpage_free_block_t* virt_next = map_temporarily_auto(block->next);
		virt_next->prev = &phys_block->next;
	}

	parent_region->buckets[order] = phys_block;
};

/**
 * Removes the given block from the appropriate bucket in the parent region.
 *
 * The parent region's lock MUST be held.
 */
static void remove_free_block(fpage_free_block_t* block) {
	fpage_free_block_t** prev = NULL;
	fpage_free_block_t* next = NULL;

	block = map_temporarily_auto(block);
	prev = map_temporarily_auto(block->prev);
	if (block->next) {
		next = map_temporarily_auto(block->next);
	}

	*prev = block->next;
	if (next) {
		next->prev = block->prev;
	}
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
 * Reads and acquires the lock for the first region at `regions_head`.
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
 * Like `acquire_next_region`, but if the given region matches the given exception region, its lock is NOT released.
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
 * The `regions_head` lock and all the region locks MUST NOT be held.
 */
static void* allocate_frame(size_t page_count, size_t* out_allocated_page_count) {
	size_t min_order = min_order_for_page_count(page_count);

	fpage_region_header_t* candidate_parent_region = NULL;
	fpage_free_block_t* candidate_block = NULL;
	size_t candidate_order = MAX_ORDER;
	uintptr_t start_split = 0;

	// first, look for the smallest usable block from any region
	for (fpage_region_header_t* phys_region = acquire_first_region(); phys_region != NULL; phys_region = acquire_next_region_with_exception(phys_region, candidate_parent_region)) {
		fpage_region_header_t* region = map_temporarily_auto(phys_region);

		for (size_t order = min_order; order < MAX_ORDER && order < candidate_order; ++order) {
			fpage_free_block_t* phys_block = region->buckets[order];

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
	remove_free_block(candidate_block);

	// we might have gotten a bigger block than we wanted. split it up.
	// the way this works can be illustrated like so:
	//
	// we found a block of 8 pages when we only wanted 1 page.
	// 1. |               8               |
	// 2. | 1 |             7             | <- 1 is the page we want; initial state before iteration
	// 3. start iterating with order = 1 (which is min_order)
	// 4. | 1 | 1 |           6           | <- 1 is marked as free
	// 5. | 1 | 1 |   2   |       4       | <- 2 is marked as free
	// 6. | 1 | 1 |   2   |       4       | <- 4 is marked as free
	// 7. stop iterating because order = 8 (which is candidate_order)
	start_split = (uintptr_t)candidate_block + (page_count_of_order(min_order) * FPAGE_PAGE_SIZE);
	for (size_t order = min_order; order < candidate_order; ++order) {
		fpage_free_block_t* phys_block = (void*)start_split;
		insert_free_block(candidate_parent_region, phys_block, page_count_of_order(order));
		start_split += (page_count_of_order(order) * FPAGE_PAGE_SIZE);
	}

	// alright, we now have the right-size block.

	// let's mark it as in-use...
	set_block_is_in_use(candidate_parent_region, candidate_block, true);

	// we can now release the parent region's lock
	flock_spin_intsafe_unlock(&((fpage_region_header_t*)map_temporarily_auto(candidate_parent_region))->lock);

	// ...let the user know how much we actually gave them (if they want to know that)...
	if (out_allocated_page_count) {
		*out_allocated_page_count = page_count_of_order(min_order);
	}

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
 * The `regions_head` lock and all the region locks MUST NOT be held.
 */
static void free_frame(void* frame, size_t page_count) {
	size_t order = min_order_for_page_count(page_count);

	fpage_region_header_t* parent_region = NULL;
	fpage_free_block_t* block = frame;

	for (fpage_region_header_t* phys_region = acquire_first_region(); phys_region != NULL; phys_region = acquire_next_region_with_exception(phys_region, parent_region)) {
		if (block_belongs_to_region(block, phys_region)) {
			parent_region = phys_region;
			break;
		}
	}

	if (!parent_region) {
		return;
	}

	// parent region's lock is held here

	// mark it as free
	set_block_is_in_use(parent_region, block, false);

	// find buddies to merge with
	for (; order < MAX_ORDER; ++order) {
		fpage_free_block_t* buddy = find_buddy(parent_region, block, page_count_of_order(order));

		// oh, no buddy? how sad :(
		if (!buddy) {
			break;
		}

		if (block_is_in_use(parent_region, buddy)) {
			// whelp, our buddy is in use. we can't do any more merging
			break;
		}

		// yay, our buddy's free! let's get together.

		// take them out of their current bucket
		remove_free_block(buddy);

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
		fpage_table_t* table = allocate_frame(fpage_round_up_page(sizeof(fpage_table_t)) / FPAGE_PAGE_SIZE, NULL);

		if (!table) {
			// oh no, looks like we don't have any more memory!
			return false;
		}

		memset(map_temporarily_auto(table), 0, fpage_round_up_page(sizeof(fpage_table_t)));

		parent->entries[index] = fpage_table_entry((uintptr_t)table, true);
		fpage_synchronize_after_table_modification();
	}

	return true;
};

static void free_table(fpage_table_t* table) {
	free_frame((void*)fpage_virtual_to_physical((uintptr_t)table), fpage_round_up_page(sizeof(fpage_table_t)) / FPAGE_PAGE_SIZE);
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

// NOTE: this function ***WILL*** overwrite existing entries!
static void map_frame_fixed(void* phys_frame, void* virt_frame, size_t page_count, fpage_page_flags_t flags) {
	uintptr_t physical_frame = (uintptr_t)phys_frame;
	uintptr_t virtual_frame = (uintptr_t)virt_frame;
	bool no_cache = flags & fpage_page_flag_no_cache;

	while (page_count > 0) {
		size_t l4_index = FPAGE_VIRT_L4(virtual_frame);
		size_t l3_index = FPAGE_VIRT_L3(virtual_frame);
		size_t l2_index = FPAGE_VIRT_L2(virtual_frame);
		size_t l1_index = FPAGE_VIRT_L1(virtual_frame);

		fpage_table_t* l4 = (fpage_table_t*)fpage_virtual_address_for_table(0, 0, 0, 0);
		fpage_table_t* l3 = (fpage_table_t*)fpage_virtual_address_for_table(1, l4_index, 0, 0);
		fpage_table_t* l2 = (fpage_table_t*)fpage_virtual_address_for_table(2, l4_index, l3_index, 0);
		fpage_table_t* l1 = (fpage_table_t*)fpage_virtual_address_for_table(3, l4_index, l3_index, l2_index);

		if (!ensure_table(l4, l4_index)) {
			return;
		}

		if (fpage_is_very_large_page_aligned(physical_frame) && fpage_is_very_large_page_aligned(virtual_frame) && page_count >= FPAGE_VERY_LARGE_PAGE_COUNT) {
			if (!fpage_entry_is_large_page_entry(l3->entries[l3_index]) && (l4_index != kernel_l4_index || l3_index != kernel_l3_index)) {
				free_table(l2);
			}

			// break the existing entry
			break_entry(2, l4_index, l3_index, 0, 0);

			// now map our entry
			l3->entries[l3_index] = fpage_very_large_page_entry(physical_frame, true);
			if (no_cache) {
				l3->entries[l3_index] = fpage_entry_disable_caching(l3->entries[l3_index]);
			}
			fpage_synchronize_after_table_modification();

			page_count -= FPAGE_VERY_LARGE_PAGE_COUNT;
			physical_frame += FPAGE_VERY_LARGE_PAGE_SIZE;
			virtual_frame += FPAGE_VERY_LARGE_PAGE_SIZE;

			continue;
		}

		if (fpage_entry_is_large_page_entry(l3->entries[l3_index])) {
			break_entry(2, l4_index, l3_index, 0, 0);

			// NOTE: this does not currently handle the case of partially remapping a large page
			//       e.g. if we want to map the first half to another location but keep the last half to where the large page pointed
			//       however, this is probably not something we'll ever want or need to do, so it's okay for now.
			//       just be aware of this limitation present here.
		}

		if (!ensure_table(l3, l3_index)) {
			return;
		}

		if (fpage_is_large_page_aligned(physical_frame) && fpage_is_large_page_aligned(virtual_frame) && page_count >= FPAGE_LARGE_PAGE_COUNT) {
			if (!fpage_entry_is_large_page_entry(l2->entries[l2_index]) && (l4_index != kernel_l4_index || l3_index != kernel_l3_index || l2_index > temporary_table_index)) {
				free_table(l1);
			}

			// break the existing entry
			break_entry(3, l4_index, l3_index, l2_index, 0);

			// now map our entry
			l2->entries[l2_index] = fpage_large_page_entry(physical_frame, true);
			if (no_cache) {
				l2->entries[l2_index] = fpage_entry_disable_caching(l2->entries[l2_index]);
			}
			fpage_synchronize_after_table_modification();

			page_count -= FPAGE_LARGE_PAGE_COUNT;
			physical_frame += FPAGE_LARGE_PAGE_SIZE;
			virtual_frame += FPAGE_LARGE_PAGE_SIZE;

			continue;
		}

		if (fpage_entry_is_large_page_entry(l2->entries[l2_index])) {
			break_entry(3, l4_index, l3_index, l2_index, 0);

			// same note as for the l3 large page case
		}

		if (!ensure_table(l2, l2_index)) {
			return;
		}

		if (l1->entries[l1_index]) {
			break_entry(4, l4_index, l3_index, l2_index, l1_index);
		}

		l1->entries[l1_index] = fpage_page_entry(physical_frame, true);
		if (no_cache) {
			l1->entries[l1_index] = fpage_entry_disable_caching(l1->entries[l1_index]);
		}
		fpage_synchronize_after_table_modification();

		page_count -= 1;
		physical_frame += FPAGE_PAGE_SIZE;
		virtual_frame += FPAGE_PAGE_SIZE;
	}
};

/**
 * Returns the bitmap bit index for the given block.
 *
 * The parent region's lock MUST be held.
 */
FERRO_ALWAYS_INLINE size_t virtual_bitmap_bit_index_for_block(const fpage_region_header_t* parent_region, const fpage_free_block_t* block) {
	uintptr_t relative_address = 0;
	relative_address = (uintptr_t)block - (uintptr_t)parent_region->start;
	return relative_address / FPAGE_PAGE_SIZE;
};

FERRO_ALWAYS_INLINE size_t virtual_byte_index_for_bit(size_t bit_index) {
	return bit_index / 8;
};

FERRO_ALWAYS_INLINE size_t virtual_byte_bit_index_for_bit(size_t bit_index) {
	return bit_index % 8;
};

/**
 * Returns a pointer to the byte where the bitmap entry for the given block is stored, as well as the bit index of the entry in this byte.
 *
 * The parent region's lock MUST be held.
 */
static const uint8_t* virtual_bitmap_entry_for_block(const fpage_region_header_t* parent_region, const fpage_free_block_t* block, size_t* out_bit_index) {
	size_t bitmap_index = virtual_bitmap_bit_index_for_block(parent_region, block);
	size_t byte_index = virtual_byte_index_for_bit(bitmap_index);
	size_t byte_bit_index = virtual_byte_bit_index_for_bit(bitmap_index);
	const uint8_t* byte = NULL;

	byte = &parent_region->bitmap[byte_index];
	*out_bit_index = byte_bit_index;

	return byte;
};

/**
 * Returns `true` if the given block is in-use.
 *
 * The parent region's lock MUST be held.
 */
static bool virtual_block_is_in_use(const fpage_region_header_t* parent_region, const fpage_free_block_t* block) {
	size_t byte_bit_index = 0;
	const uint8_t* byte = virtual_bitmap_entry_for_block(parent_region, block, &byte_bit_index);

	return (*byte) & (1 << byte_bit_index);
};

/**
 * Sets whether the given block is in-use.
 *
 * The parent region's lock MUST be held.
 */
static void set_virtual_block_is_in_use(fpage_region_header_t* parent_region, const fpage_free_block_t* block, bool in_use) {
	size_t byte_bit_index = 0;
	uint8_t* byte = (uint8_t*)virtual_bitmap_entry_for_block(parent_region, block, &byte_bit_index);

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
static void insert_virtual_free_block(fpage_region_header_t* parent_region, fpage_free_block_t* block, size_t block_page_count) {
	size_t order = max_order_of_page_count(block_page_count);
	fpage_free_block_t* phys_block = allocate_frame(fpage_round_up_page(sizeof(fpage_free_block_t)) / FPAGE_PAGE_SIZE, NULL);

	map_frame_fixed(phys_block, block, fpage_round_up_page(sizeof(fpage_free_block_t)) / FPAGE_PAGE_SIZE, 0);

	block->prev = &parent_region->buckets[order];
	block->next = parent_region->buckets[order];

	if (block->next) {
		block->next->prev = &block->next;
	}

	parent_region->buckets[order] = block;
};

/**
 * Removes the given block from the appropriate bucket in the parent region.
 *
 * The parent region's lock MUST be held.
 */
static void remove_virtual_free_block(fpage_free_block_t* block) {
	*block->prev = block->next;
	if (block->next) {
		block->next->prev = block->prev;
	}

	free_frame((void*)fpage_virtual_to_physical((uintptr_t)block), fpage_round_up_page(sizeof(fpage_free_block_t)) / FPAGE_PAGE_SIZE);
};

/**
 * Finds the region's buddy.
 *
 * The parent region's lock MUST be held.
 */
static fpage_free_block_t* find_virtual_buddy(fpage_region_header_t* parent_region, fpage_free_block_t* block, size_t block_page_count) {
	uintptr_t parent_start = (uintptr_t)parent_region->start;
	uintptr_t maybe_buddy = (((uintptr_t)block - parent_start) ^ (block_page_count * FPAGE_PAGE_SIZE)) + parent_start;

	if (maybe_buddy + (block_page_count * FPAGE_PAGE_SIZE) > parent_start + (parent_region->page_count * FPAGE_PAGE_SIZE)) {
		return NULL;
	}

	return (fpage_free_block_t*)maybe_buddy;
};

/**
 * Reads and acquires the lock for the first region at `regions_head`.
 *
 * The `kernel_virtual_regions_head` lock and the first region's lock MUST NOT be held.
 */
static fpage_region_header_t* virtual_acquire_first_region(void) {
	fpage_region_header_t* region;
	flock_spin_intsafe_lock(&kernel_virtual_regions_head_lock);
	region = kernel_virtual_regions_head;
	if (region) {
		flock_spin_intsafe_lock(&region->lock);
	}
	flock_spin_intsafe_unlock(&kernel_virtual_regions_head_lock);
	return region;
};

/**
 * Reads and acquires the lock the lock for the next region after the given region.
 * Afterwards, it releases the lock for the given region.
 *
 * The given region's lock MUST be held and next region's lock MUST NOT be held.
 */
static fpage_region_header_t* virtual_acquire_next_region(fpage_region_header_t* prev) {
	fpage_region_header_t* next = prev->next;
	if (next) {
		flock_spin_intsafe_lock(&next->lock);
	}
	flock_spin_intsafe_unlock(&prev->lock);
	return next;
};

/**
 * Like `acquire_next_region`, but if the given region matches the given exception region, its lock is NOT released.
 */
static fpage_region_header_t* virtual_acquire_next_region_with_exception(fpage_region_header_t* prev, fpage_region_header_t* exception) {
	fpage_region_header_t* next = prev->next;
	if (next) {
		flock_spin_intsafe_lock(&next->lock);
	}
	if (prev != exception) {
		flock_spin_intsafe_unlock(&prev->lock);
	}
	return next;
};

/**
 * Allocates a virtual region of the given size.
 *
 * The `kernel_virtual_regions_head_lock` lock and all the region locks MUST NOT be held.
 */
static void* allocate_virtual(size_t page_count, size_t* out_allocated_page_count, bool user) {
	size_t min_order = min_order_for_page_count(page_count);

	fpage_region_header_t* candidate_parent_region = NULL;
	fpage_free_block_t* candidate_block = NULL;
	size_t candidate_order = MAX_ORDER;
	uintptr_t start_split = 0;

	// first, look for the smallest usable block from any region
	for (fpage_region_header_t* region = virtual_acquire_first_region(); region != NULL; region = virtual_acquire_next_region_with_exception(region, candidate_parent_region)) {
		for (size_t order = min_order; order < MAX_ORDER && order < candidate_order; ++order) {
			fpage_free_block_t* block = region->buckets[order];

			if (block) {
				if (candidate_parent_region) {
					flock_spin_intsafe_unlock(&candidate_parent_region->lock);
				}
				candidate_order = order;
				candidate_block = block;
				candidate_parent_region = region;
				break;
			}
		}

		if (candidate_order == min_order) {
			break;
		}
	}

	// uh-oh, we don't have any free blocks big enough in any region
	if (!candidate_block) {
		return NULL;
	}

	// the candidate parent region's lock is held here

	// okay, we've chosen our candidate region. un-free it
	remove_virtual_free_block(candidate_block);

	// we might have gotten a bigger block than we wanted. split it up.
	// to understand how this works, see `allocate_frame`.
	start_split = (uintptr_t)candidate_block + (page_count_of_order(min_order) * FPAGE_PAGE_SIZE);
	for (size_t order = min_order; order < candidate_order; ++order) {
		fpage_free_block_t* block = (void*)start_split;
		insert_virtual_free_block(candidate_parent_region, block, page_count_of_order(order));
		start_split += (page_count_of_order(order) * FPAGE_PAGE_SIZE);
	}

	// alright, we now have the right-size block.

	// let's mark it as in-use...
	set_virtual_block_is_in_use(candidate_parent_region, candidate_block, true);

	// drop the parent region lock
	flock_spin_intsafe_unlock(&candidate_parent_region->lock);

	// ...let the user know how much we actually gave them (if they want to know that)...
	if (out_allocated_page_count) {
		*out_allocated_page_count = page_count_of_order(min_order);
	}

	// ...and finally, give them their new block
	return candidate_block;
};

/**
 * Returns `true` if the given block belongs to the given region.
 *
 * The region's lock MUST be held.
 */
FERRO_ALWAYS_INLINE bool virtual_block_belongs_to_region(fpage_free_block_t* block, fpage_region_header_t* region) {
	return (uintptr_t)block >= (uintptr_t)region->start && (uintptr_t)block < (uintptr_t)region->start + (region->page_count * FPAGE_PAGE_SIZE);
};

/**
 * Frees a virtual region of the given size.
 *
 * The `kernel_virtual_regions_head_lock` lock and all the region locks MUST NOT be held.
 */
static void free_virtual(void* virtual, size_t page_count, bool user) {
	size_t order = min_order_for_page_count(page_count);

	fpage_region_header_t* parent_region = NULL;
	fpage_free_block_t* block = virtual;

	for (fpage_region_header_t* region = virtual_acquire_first_region(); region != NULL; region = virtual_acquire_next_region_with_exception(region, parent_region)) {
		if (virtual_block_belongs_to_region(block, region)) {
			parent_region = region;
			break;
		}
	}

	if (!parent_region) {
		return;
	}

	// the parent region's lock is held here

	// mark it as free
	set_block_is_in_use(parent_region, block, false);

	// find buddies to merge with
	for (; order < MAX_ORDER; ++order) {
		fpage_free_block_t* buddy = find_virtual_buddy(parent_region, block, page_count_of_order(order));

		// oh, no buddy? how sad :(
		if (!buddy) {
			break;
		}

		if (virtual_block_is_in_use(parent_region, buddy)) {
			// whelp, our buddy is in use. we can't do any more merging
			break;
		}

		// yay, our buddy's free! let's get together.

		// take them out of their current bucket
		remove_virtual_free_block(buddy);

		// whoever's got the lower address is the start of the bigger block
		if (buddy < block) {
			block = buddy;
		}

		// now *don't* insert the new block into the free list.
		// that would be pointless since we might still have a buddy to merge with the bigger block
		// and we insert it later, after the loop.
	}

	// finally, insert the new (possibly merged) block into the appropriate bucket
	insert_virtual_free_block(parent_region, block, page_count_of_order(order));

	// drop the parent region's lock
	flock_spin_intsafe_unlock(&parent_region->lock);
};

// we don't need to worry about locks in this function; interrupts are disabled and we're in a uniprocessor environment
void fpage_init(size_t next_l2, fpage_table_t* table, ferro_memory_region_t* memory_regions, size_t memory_region_count, void* image_base) {
	fpage_table_t* l2_table = NULL;
	uintptr_t virt_start = FERRO_KERNEL_VIRTUAL_START;
	size_t total_phys_page_count = 0;
	size_t max_virt_page_count = 0;
	size_t total_virt_page_count = 0;

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
	// can't use `fpage_virtual_to_physical` for the physical address lookup because it depends on the recursive entry (which is what we're setting up right now).
	root_table->entries[root_recursive_index] = fpage_table_entry(FERRO_KERNEL_VIRT_TO_PHYS(root_table) + (uintptr_t)image_base, true);
	fpage_synchronize_after_table_modification();

	// we can use the recursive virtual address for the table now
	root_table = (fpage_table_t*)fpage_virtual_address_for_table(0, 0, 0, 0);

	// set up the temporary table
	l2_table = (fpage_table_t*)fpage_virtual_address_for_table(2, FPAGE_VIRT_L4(FERRO_KERNEL_VIRTUAL_START), FPAGE_VIRT_L3(FERRO_KERNEL_VIRTUAL_START), 0);
	l2_table->entries[temporary_table_index = next_l2] = fpage_table_entry(fpage_virtual_to_physical((uintptr_t)&temporary_table), true);
	fpage_synchronize_after_table_modification();

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

		/*for (size_t j = i; j < memory_region_count; ++j) {
			
		}*/

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
		memset(&header->bitmap[0], 0, HEADER_BITMAP_SPACE);
		for (size_t i = 0; i < extra_bitmap_page_count; ++i) {
			uint8_t* page = (uint8_t*)map_temporarily_auto((void*)(physical_start + FPAGE_PAGE_SIZE + (i * FPAGE_PAGE_SIZE)));
			memset(page, 0, FPAGE_PAGE_SIZE);
		}

		// clear out the buckets
		memset(&header->buckets[0], 0, sizeof(header->buckets));

		while (pages_allocated < page_count) {
			size_t order = max_order_of_page_count(page_count - pages_allocated);
			size_t pages = page_count_of_order(order);
			void* phys_addr = (void*)((uintptr_t)usable_start + (pages_allocated * FPAGE_PAGE_SIZE));

			insert_free_block((void*)physical_start, phys_addr, pages);

			pages_allocated += pages;
		}
	}

	// next we need to enumerate and set up available virtual memory regions
	//
	// for now, we only need to set up the kernel address space

	// also, determine the maximum amount of virtual memory the buddy allocator can use.
	// this is based on `total_phys_page_count`, which is the total amount of *usable* physical memory
	// (i.e. we might have more than `total_phys_page_count`, but it's unusable).
	max_virt_page_count = total_phys_page_count * MAX_VIRTUAL_KERNEL_BUDDY_ALLOCATOR_PAGE_COUNT_COEFFICIENT;

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

			// don't touch the recursive entry
			if (l4_index == root_recursive_index) {
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

					// skip the temporary table
					if (l4_index == kernel_l4_index && l3_index == kernel_l3_index && l2_index == temporary_table_index) {
						l1_index = 0;
						continue;
					}

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

					// the temporary table counts as non-free
					if (l4_index == kernel_l4_index && l3_index == kernel_l3_index && l2_index == temporary_table_index) {
						goto done_determining_size;
					}

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

		phys_header = allocate_frame(fpage_round_up_page(sizeof(fpage_region_header_t)) / FPAGE_PAGE_SIZE, NULL);

		if (!phys_header) {
			// crap. we're out of physical memory.
			// it's unlikely we'll be able to satisfy future requests, but skip this region and continue with the next anyways.
			virt_start += (1 + virt_page_count + extra_bitmap_page_count) * FPAGE_PAGE_SIZE;
			continue;
		}

		header = (fpage_region_header_t*)virt_start;

		map_frame_fixed(phys_header, header, fpage_round_up_page(sizeof(fpage_region_header_t)) / FPAGE_PAGE_SIZE, 0);

		header->prev = &kernel_virtual_regions_head;
		header->next = kernel_virtual_regions_head;
		if (header->next) {
			header->next->prev = &header->next;
		}
		header->page_count = virt_page_count;
		header->start = usable_start = (void*)(virt_start + FPAGE_PAGE_SIZE + (extra_bitmap_page_count * FPAGE_PAGE_SIZE));

		flock_spin_intsafe_init(&header->lock);

		kernel_virtual_regions_head = header;

		// clear out the bitmap
		memset(&header->bitmap[0], 0, HEADER_BITMAP_SPACE);
		for (size_t i = 0; i < extra_bitmap_page_count; ++i) {
			uint8_t* phys_page = allocate_frame(1, NULL);
			uint8_t* page = (uint8_t*)(virt_start + FPAGE_PAGE_SIZE + (i * FPAGE_PAGE_SIZE));

			if (!phys_page) {
				// ack. we've gotta undo all the work we've done up 'till now.
				for (; i > 0; --i) {
					uint8_t* page = (uint8_t*)(virt_start + FPAGE_PAGE_SIZE + ((i - 1) * FPAGE_PAGE_SIZE));
					free_frame((void*)fpage_virtual_to_physical((uintptr_t)page), 1);
					break_entry(4, FPAGE_VIRT_L4(page), FPAGE_VIRT_L3(page), FPAGE_VIRT_L2(page), FPAGE_VIRT_L1(page));
				}

				failed_to_allocate_bitmap = true;
				break;
			}

			map_frame_fixed(phys_page, page, 1, 0);

			memset(page, 0, FPAGE_PAGE_SIZE);
		}

		if (failed_to_allocate_bitmap) {
			free_frame((void*)fpage_virtual_to_physical((uintptr_t)header), fpage_round_up_page(sizeof(fpage_region_header_t)) / FPAGE_PAGE_SIZE);
			break_entry(4, FPAGE_VIRT_L4(header), FPAGE_VIRT_L3(header), FPAGE_VIRT_L2(header), FPAGE_VIRT_L1(header));
			virt_start += (1 + virt_page_count + extra_bitmap_page_count) * FPAGE_PAGE_SIZE;
			continue;
		}

		// clear out the buckets
		memset(&header->buckets[0], 0, sizeof(header->buckets));

		while (pages_allocated < virt_page_count) {
			size_t order = max_order_of_page_count(virt_page_count - pages_allocated);
			size_t pages = page_count_of_order(order);
			void* addr = (void*)((uintptr_t)usable_start + (pages_allocated * FPAGE_PAGE_SIZE));

			insert_virtual_free_block(header, addr, pages);

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
};

ferr_t fpage_map_kernel_any(void* physical_address, size_t page_count, void** out_virtual_address, fpage_page_flags_t flags) {
	void* virt = NULL;

	if (physical_address == NULL || page_count == 0 || page_count == SIZE_MAX || out_virtual_address == NULL) {
		return ferr_invalid_argument;
	}

	virt = allocate_virtual(page_count, NULL, false);

	if (!virt) {
		return ferr_temporary_outage;
	}

	map_frame_fixed(physical_address, virt, page_count, flags);

	*out_virtual_address = virt;

	return ferr_ok;
};

ferr_t fpage_unmap_kernel(void* virtual_address, size_t page_count) {
	if (virtual_address == NULL || page_count == 0 || page_count == SIZE_MAX) {
		return ferr_invalid_argument;
	}

	// TODO: invalidate the entries

	free_virtual(virtual_address, page_count, false);

	return ferr_ok;
};

ferr_t fpage_allocate_kernel(size_t page_count, void** out_virtual_address) {
	void* virt = NULL;

	if (page_count == 0 || page_count == SIZE_MAX || out_virtual_address == NULL) {
		return ferr_invalid_argument;
	}

	virt = allocate_virtual(page_count, NULL, false);

	if (!virt) {
		return ferr_temporary_outage;
	}

	for (size_t i = 0; i < page_count; ++i) {
		void* frame = allocate_frame(1, NULL);

		if (!frame) {
			for (; i > 0; --i) {
				free_frame((void*)fpage_virtual_to_physical((uintptr_t)virt + ((i - 1) * FPAGE_PAGE_SIZE)), 1);
			}
			return ferr_temporary_outage;
		}

		map_frame_fixed(frame, (void*)((uintptr_t)virt + (i * FPAGE_PAGE_SIZE)), 1, 0);
	}

	*out_virtual_address = virt;

	return ferr_ok;
};

ferr_t fpage_free_kernel(void* virtual_address, size_t page_count) {
	if (virtual_address == NULL || page_count == 0 || page_count == SIZE_MAX) {
		return ferr_invalid_argument;
	}

	for (size_t i = 0; i < page_count; ++i) {
		free_frame((void*)fpage_virtual_to_physical((uintptr_t)virtual_address + (i * FPAGE_PAGE_SIZE)), 1);
	}

	// TODO: unmap the region

	free_virtual(virtual_address, page_count, false);

	return ferr_ok;
};
