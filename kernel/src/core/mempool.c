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
 * Kernel memory pool management (e.g. de/allocation).
 */

#include <stdint.h>
#include <stdbool.h>

#include <ferro/core/mempool.h>
#include <ferro/bits.h>
#include <ferro/core/paging.h>
#include <ferro/core/panic.h>
#include <ferro/core/locks.h>
#include <libsimple/libsimple.h>

// maximum order of a single allocation
#define MAX_ORDER 32

// size of a single leaf in bytes, including the header
#define LEAF_SIZE 16

// pointer value returned for allocations of size 0
#define NO_BYTES_POINTER ((void*)UINTPTR_MAX)

// how many regions to keep when freeing completely unused regions
#define KEPT_REGION_COUNT 3

// alright, so, each block here needs a byte for bookkeeping.
// the overhead of this bookkeeping is 6.25% of the total memory for a region.
// e.g. with a leaf size of 16 bytes, a region of 64KiB would require an additional 4KiB for bookkeeping.
// yikes. but that's the cost of allowing free() without needing the memory size!

FERRO_STRUCT(fmempool_free_leaf) {
	fmempool_free_leaf_t** prev;
	fmempool_free_leaf_t* next;
};

FERRO_STRUCT(fmempool_region_header) {
	fmempool_region_header_t** prev;
	fmempool_region_header_t* next;
	size_t leaf_count;
	size_t free_count;
	void* start;
	fmempool_free_leaf_t* buckets[MAX_ORDER];

	// this lock protects the entire region (including leaves) from both reads and writes
	flock_spin_intsafe_t lock;

	// each byte has the following layout:
	//   * top bit (bit 7) = in-use bit (1 if in-use, 0 otherwise)
	//   * bottom 5 bits (bits 0-4) = order (value from 0-31, inclusive)
	// all other bits are currently unused, but may be in the future
	uint8_t bookkeeping[];
};

#define HEADER_BOOKKEEPING_COUNT (FPAGE_PAGE_SIZE - sizeof(fmempool_region_header_t))

static fmempool_region_header_t* regions_head = NULL;

/**
 * Protects ::regions_head from both reads and writes.
 */
static flock_spin_intsafe_t regions_head_lock = FLOCK_SPIN_INTSAFE_INIT;

FERRO_ALWAYS_INLINE uint64_t fmempool_round_up_leaf(uint64_t number) {
	return (number + LEAF_SIZE - 1) & -LEAF_SIZE;
};

FERRO_ALWAYS_INLINE size_t leaf_count_of_order(size_t order) {
	return 1ULL << order;
};

#define MAX_ALLOCATION_SIZE (leaf_count_of_order(MAX_ORDER) * LEAF_SIZE)

FERRO_ALWAYS_INLINE size_t min_order_for_leaf_count(size_t leaf_count) {
	if (leaf_count == 0) {
		return SIZE_MAX;
	} else {
		size_t result = ferro_bits_in_use_u64(leaf_count) - 1;
		if (result >= MAX_ORDER) {
			return MAX_ORDER - 1;
		}
		return (leaf_count > leaf_count_of_order(result)) ? (result + 1) : result;
	}
};

FERRO_ALWAYS_INLINE size_t max_order_of_leaf_count(size_t leaf_count) {
	if (leaf_count == 0) {
		return SIZE_MAX;
	} else {
		size_t result = ferro_bits_in_use_u64(leaf_count) - 1;
		if (result >= MAX_ORDER) {
			return MAX_ORDER - 1;
		}
		return result;
	}
};

FERRO_ALWAYS_INLINE size_t min_order_for_byte_count(size_t byte_count) {
	return min_order_for_leaf_count(fmempool_round_up_leaf(byte_count) / LEAF_SIZE);
};

/**
 * Returns the given leaf's order.
 *
 * The parent region's lock MUST be held.
 */
FERRO_ALWAYS_INLINE size_t leaf_index(const fmempool_region_header_t* parent_region, const fmempool_free_leaf_t* leaf) {
	return ((uintptr_t)leaf - (uintptr_t)parent_region->start) / LEAF_SIZE;
};

/**
 * Returns whether the given leaf is in-use or not.
 *
 * The parent region's lock MUST be held.
 */
static bool leaf_is_in_use(const fmempool_region_header_t* parent_region, const fmempool_free_leaf_t* leaf) {
	return (parent_region->bookkeeping[leaf_index(parent_region, leaf)] & (1 << 7)) != 0;
};

/**
 * Sets whether the given leaf is in-use or not.
 *
 * The parent region's lock MUST be held.
 */
static void set_leaf_is_in_use(fmempool_region_header_t* parent_region, const fmempool_free_leaf_t* leaf, bool is_in_use) {
	uint8_t* byte = &parent_region->bookkeeping[leaf_index(parent_region, leaf)];
	if (is_in_use) {
		*byte |= 1 << 7;
	} else {
		*byte &= ~(1 << 7);
	}
};

/**
 * Returns the given leaf's order.
 *
 * The parent region's lock MUST be held.
 */
static size_t leaf_order(const fmempool_region_header_t* parent_region, const fmempool_free_leaf_t* leaf) {
	return parent_region->bookkeeping[leaf_index(parent_region, leaf)] & 0x1f;
};

/**
 * Sets the given leaf's order.
 *
 * The parent region's lock MUST be held.
 */
static void set_leaf_order(fmempool_region_header_t* parent_region, const fmempool_free_leaf_t* leaf, size_t order) {
	uint8_t* byte = &parent_region->bookkeeping[leaf_index(parent_region, leaf)];
	*byte = (*byte & ~(0x1f)) | (order & 0x1f);
};

/**
 * Inserts the given leaf into the appropriate bucket in the parent region.
 *
 * The parent region's lock MUST be held.
 */
static void insert_free_leaf(fmempool_region_header_t* parent_region, fmempool_free_leaf_t* leaf, size_t order) {
	leaf->prev = &parent_region->buckets[order];
	leaf->next = parent_region->buckets[order];

	if (leaf->next) {
		leaf->next->prev = &leaf->next;
	}

	parent_region->buckets[order] = leaf;

	set_leaf_order(parent_region, leaf, order);

	parent_region->free_count += leaf_count_of_order(order);

	set_leaf_is_in_use(parent_region, leaf, false);
};

/**
 * Removes the given leaf from the appropriate bucket in the parent region.
 *
 * The parent region's lock MUST be held.
 */
static void remove_free_leaf(fmempool_region_header_t* parent_region, fmempool_free_leaf_t* leaf, size_t order) {
	*leaf->prev = leaf->next;
	if (leaf->next) {
		leaf->next->prev = leaf->prev;
	}

	parent_region->free_count -= leaf_count_of_order(order);

	set_leaf_is_in_use(parent_region, leaf, true);
};

/**
 * Finds the given leaf's buddy.
 *
 * The parent region's lock MUST be held.
 */
static fmempool_free_leaf_t* find_buddy(fmempool_region_header_t* parent_region, fmempool_free_leaf_t* leaf, size_t order) {
	uintptr_t parent_start = (uintptr_t)parent_region->start;
	size_t leaf_count = leaf_count_of_order(order);
	uintptr_t maybe_buddy = (((uintptr_t)leaf - parent_start) ^ (leaf_count * LEAF_SIZE)) + parent_start;

	if (maybe_buddy + (leaf_count * LEAF_SIZE) > parent_start + (parent_region->leaf_count * LEAF_SIZE)) {
		return NULL;
	}

	return (fmempool_free_leaf_t*)maybe_buddy;
};

/**
 * Returns the necessary region size for the given leaf count, including header and bookkeeping info space.
 */
static size_t region_size_for_leaf_count(size_t leaf_count, size_t* out_extra_bookkeeping_page_count) {
	size_t region_size = fpage_round_up_page(leaf_count * LEAF_SIZE);
	size_t extra_bookkeeping_page_count = 0;

	if (leaf_count > HEADER_BOOKKEEPING_COUNT) {
		extra_bookkeeping_page_count = fpage_round_up_page(leaf_count - HEADER_BOOKKEEPING_COUNT) / FPAGE_PAGE_SIZE;
		region_size += extra_bookkeeping_page_count * FPAGE_PAGE_SIZE;
	}

	if (out_extra_bookkeeping_page_count) {
		*out_extra_bookkeeping_page_count = extra_bookkeeping_page_count;
	}

	region_size += FPAGE_PAGE_SIZE;

	return region_size;
};

/**
 * Reads and acquires the lock for the first region at ::regions_head.
 *
 * The ::region_head lock and the first region's lock MUST NOT be held.
 */
static fmempool_region_header_t* acquire_first_region(void) {
	fmempool_region_header_t* region;
	flock_spin_intsafe_lock(&regions_head_lock);
	region = regions_head;
	if (region) {
		flock_spin_intsafe_lock(&region->lock);
	}
	flock_spin_intsafe_unlock(&regions_head_lock);
	return region;
};

/**
 * Reads and acquires the lock the lock for the next region after the given region.
 * Afterwards, it releases the lock for the given region.
 *
 * The given region's lock MUST be held and next region's lock MUST NOT be held.
 */
static fmempool_region_header_t* acquire_next_region(fmempool_region_header_t* prev) {
	fmempool_region_header_t* next = prev->next;
	if (next) {
		flock_spin_intsafe_lock(&next->lock);
	}
	flock_spin_intsafe_unlock(&prev->lock);
	return next;
};

/**
 * Like acquire_next_region(), but if the given region matches the given exception region, its lock is NOT released.
 */
static fmempool_region_header_t* acquire_next_region_with_exception(fmempool_region_header_t* prev, fmempool_region_header_t* exception) {
	fmempool_region_header_t* next = prev->next;
	if (next) {
		flock_spin_intsafe_lock(&next->lock);
	}
	if (prev != exception) {
		flock_spin_intsafe_unlock(&prev->lock);
	}
	return next;
};

/**
 * Returns a pointer to the previous region of the given region.
 *
 * Care must be taken to ensure the given region is NOT the first region in the list.
 *
 * The region's lock MUST be held.
 */
FERRO_ALWAYS_INLINE fmempool_region_header_t* region_from_prev(fmempool_region_header_t* region) {
	return (fmempool_region_header_t*)((uintptr_t)region->prev - offsetof(fmempool_region_header_t, next));
};

/**
 * Removes the region from the given list.
 *
 * The region's lock MUST be held and the head lock, previous region's lock, and next region's lock MUST NOT be held.
 */
static void remove_region(fmempool_region_header_t* region, fmempool_region_header_t** head, flock_spin_intsafe_t* head_lock) {
	if (region->prev == head) {
		flock_spin_intsafe_lock(head_lock);
	} else {
		flock_spin_intsafe_lock(&region_from_prev(region)->lock);
	}

	if (region->next) {
		flock_spin_intsafe_lock(&region->next->lock);
	}

	*region->prev = region->next;
	if (region->next) {
		region->next->prev = region->prev;
	}

	if (region->next) {
		flock_spin_intsafe_unlock(&region->next->lock);
	}

	if (region->prev == head) {
		flock_spin_intsafe_unlock(head_lock);
	} else {
		flock_spin_intsafe_unlock(&region_from_prev(region)->lock);
	}
};

/**
 * Inserts the given region into the given region list.
 *
 * The region's lock MUST be held and the region list's lock and the first region's lock MUST NOT be held.
 */
static void insert_region(fmempool_region_header_t* region, fmempool_region_header_t** head, flock_spin_intsafe_t* head_lock) {
	fmempool_region_header_t* old_first;

	flock_spin_intsafe_lock(head_lock);

	old_first = *head;

	if (old_first) {
		flock_spin_intsafe_lock(&old_first->lock);
		old_first->prev = &region->next;
		flock_spin_intsafe_unlock(&old_first->lock);
	}

	region->prev = head;
	region->next = old_first;
	*head = region;

	flock_spin_intsafe_unlock(head_lock);
};

/**
 * Iterates through the list of regions and frees unnecessary regions.
 *
 * This function tries to keep the `n` largest regions, where `n` is ::KEPT_REGION_COUNT.
 *
 * Must be called with the ::regions_head lock and all region locks unheld.
 */
static void do_region_free(void) {
	fmempool_region_header_t* kept[KEPT_REGION_COUNT] = {0};
	fmempool_region_header_t* free_these = NULL;
	flock_spin_intsafe_t free_lock = FLOCK_SPIN_INTSAFE_INIT;

	// first, find the free regions
	for (fmempool_region_header_t* region = acquire_first_region(); region != NULL;) {
		fmempool_region_header_t* next_region;
		bool kept_it = false;

		// not completely unused; skip it
		if (region->free_count != region->leaf_count) {
			region = acquire_next_region(region);
			continue;
		}

		// no matter what we do next, we're going to remove this region from the region list
		remove_region(region, &regions_head, &regions_head_lock);

		// go ahead and release the lock on this one and acquire the next one
		// we know that no one else has access to the current region because we removed it from the region list
		next_region = acquire_next_region(region);

		for (size_t i = 0; i < KEPT_REGION_COUNT; ++i) {
			if (!kept[i]) {
				kept[i] = region;
				kept_it = true;
				break;
			}

			if (kept[i]->leaf_count < region->leaf_count) {
				// insert the previously kept region into the free list
				insert_region(kept[i], &free_these, &free_lock);
				kept[i] = region;
				kept_it = true;
				break;
			}
		}

		// if we're not going to keep it, add it to the list of regions to free
		if (!kept_it) {
			insert_region(region, &free_these, &free_lock);
		}

		region = next_region;
	}

	// add the regions we decided to keep back into the region list
	for (size_t i = 0; i < KEPT_REGION_COUNT; ++i) {
		if (!kept[i]) {
			continue;
		}
		insert_region(kept[i], &regions_head, &regions_head_lock);
	}

	// now free the others
	// no need to bother with locks here; no one else has access to these regions
	for (fmempool_region_header_t* region = free_these; region != NULL;) {
		fmempool_region_header_t* next_region = region->next;

		// free it
		if (fpage_free_kernel(region, region_size_for_leaf_count(region->leaf_count, NULL)) != ferr_ok) {
			// huh. something's up.
			// just re-insert it for now.
			// TODO: maybe panic here?

			insert_region(region, &regions_head, &regions_head_lock);
		}

		region = next_region;
	}
};

/**
 * Attempts to fulfill the given allocation using an existing region already present in the region list.
 *
 * The ::regions_head lock and all of the region locks MUST NOT be held.
 */
static void* allocate_existing(size_t byte_count) {
	size_t min_order = min_order_for_byte_count(byte_count);

	fmempool_region_header_t* candidate_parent_region = NULL;
	fmempool_free_leaf_t* candidate_leaf = NULL;
	size_t candidate_order = MAX_ORDER;
	uintptr_t start_split = 0;

	// first, look for the smallest usable block from any region
	for (fmempool_region_header_t* region = acquire_first_region(); region != NULL; region = acquire_next_region_with_exception(region, candidate_parent_region)) {
		for (size_t order = min_order; order < MAX_ORDER && order < candidate_order; ++order) {
			fmempool_free_leaf_t* leaf = region->buckets[order];

			if (leaf) {
				if (candidate_parent_region) {
					flock_spin_intsafe_unlock(&candidate_parent_region->lock);
				}
				candidate_order = order;
				candidate_leaf = leaf;
				candidate_parent_region = region;
				break;
			}
		}

		if (candidate_order == min_order) {
			break;
		}
	}

	// no free blocks big enough in any region
	if (!candidate_leaf) {
		return NULL;
	}

	// the candidate parent region's lock is still held here

	remove_free_leaf(candidate_parent_region, candidate_leaf, candidate_order);

	// we might have gotten a bigger block than we wanted. split it up.
	// to understand how this works, see allocate_frame() in `paging.c`.
	start_split = (uintptr_t)candidate_leaf + (leaf_count_of_order(min_order) * LEAF_SIZE);
	for (size_t order = min_order; order < candidate_order; ++order) {
		fmempool_free_leaf_t* leaf = (void*)start_split;
		insert_free_leaf(candidate_parent_region, leaf, order);
		start_split += (leaf_count_of_order(order) * LEAF_SIZE);
	}

	set_leaf_order(candidate_parent_region, candidate_leaf, min_order);
	set_leaf_is_in_use(candidate_parent_region, candidate_leaf, true);

	flock_spin_intsafe_unlock(&candidate_parent_region->lock);

	return candidate_leaf;
};

/**
 * Allocates a brand new region for the given allocation and inserts it into the region list.
 *
 * The ::regions_head lock and the first region's lock MUST NOT be held.
 */
static void* allocate_new(size_t byte_count) {
	size_t min_order = min_order_for_byte_count(byte_count);
	size_t region_order = min_order * 4;
	fmempool_region_header_t* header = NULL;
	size_t region_size = 0;
	size_t extra_bookkeeping_page_count = 0;
	size_t leaves_allocated = 0;
	size_t leaf_count = 0;

	while (region_order > MAX_ORDER) {
		region_order /= 2;
	}

try_order:
	leaf_count = leaf_count_of_order(region_order);
	region_size = region_size_for_leaf_count(leaf_count, &extra_bookkeeping_page_count);

	if (fpage_allocate_kernel(fpage_round_up_page(region_size) / FPAGE_PAGE_SIZE, (void**)&header, 0) != ferr_ok) {
		// can't go lower our minimum order
		if (region_order == min_order) {
			return NULL;
		}

		// otherwise, try half of our previous order
		region_order /= 2;
		goto try_order;
	}

	flock_spin_intsafe_init(&header->lock);
	flock_spin_intsafe_lock(&header->lock);

	insert_region(header, &regions_head, &regions_head_lock);

	header->leaf_count = leaf_count;

	header->start = (void*)((uintptr_t)header + ((extra_bookkeeping_page_count + 1) * FPAGE_PAGE_SIZE));

	simple_memset(&header->bookkeeping[0], 0, HEADER_BOOKKEEPING_COUNT);
	for (size_t i = 0; i < extra_bookkeeping_page_count; ++i) {
		uint8_t* page = (uint8_t*)header + FPAGE_PAGE_SIZE + (i * FPAGE_PAGE_SIZE);
		simple_memset(page, 0, FPAGE_PAGE_SIZE);
	}

	simple_memset(&header->buckets[0], 0, sizeof(header->buckets));

	while (leaves_allocated < leaf_count) {
		size_t order = max_order_of_leaf_count(leaf_count - leaves_allocated);
		size_t leaves = leaf_count_of_order(order);
		void* addr = (void*)((uintptr_t)header->start + (leaves_allocated * LEAF_SIZE));

		insert_free_leaf(header, addr, order);

		leaves_allocated += leaves;
	}

	flock_spin_intsafe_unlock(&header->lock);

	// alright, this has to succeed now
	return allocate_existing(byte_count);
};

/**
 * Returns `true` if the given leaf belongs to the given region. The region's lock MUST be held.
 */
FERRO_ALWAYS_INLINE bool leaf_belongs_to_region(fmempool_free_leaf_t* leaf, fmempool_region_header_t* region) {
	return (uintptr_t)leaf >= (uintptr_t)region->start && (uintptr_t)leaf < (uintptr_t)region->start + (region->leaf_count * LEAF_SIZE);
};

/**
 * Finds the leaf's parent region and returns it with its lock held.
 */
static fmempool_region_header_t* find_parent_region(fmempool_free_leaf_t* leaf) {
	fmempool_region_header_t* parent_region = NULL;
	for (fmempool_region_header_t* region = acquire_first_region(); region != NULL; region = acquire_next_region(region)) {
		if (leaf_belongs_to_region(leaf, region)) {
			parent_region = region;
			break;
		}
	}
	return parent_region;
};

static bool free_leaf(void* address) {
	size_t order = 0;
	fmempool_free_leaf_t* leaf = address;
	fmempool_region_header_t* parent_region = find_parent_region(leaf);
	bool is_free = false;

	if (!parent_region) {
		return false;
	}

	// parent region's lock is held here

	order = leaf_order(parent_region, leaf);

	// find buddies to merge with
	for (; order < MAX_ORDER; ++order) {
		fmempool_free_leaf_t* buddy = find_buddy(parent_region, leaf, order);

		// oh, no buddy? how sad :(
		if (!buddy) {
			break;
		}

		if (leaf_is_in_use(parent_region, buddy)) {
			// whelp, our buddy is in use. we can't do any more merging
			break;
		}

		// yay, our buddy's free! let's get together.

		// take them out of their current bucket
		remove_free_leaf(parent_region, buddy, order);

		// whoever's got the lower address is the start of the bigger block
		if (buddy < leaf) {
			leaf = buddy;
		}

		// now *don't* insert the new block into the free list.
		// that would be pointless since we might still have a buddy to merge with the bigger block
		// and we insert it later, after the loop.
	}

	// finally, insert the new (possibly merged) block into the appropriate bucket
	// (this will also take care of updating the leaf's order in the bookkeeping info)
	insert_free_leaf(parent_region, leaf, order);

	is_free = parent_region->free_count == parent_region->leaf_count;

	flock_spin_intsafe_unlock(&parent_region->lock);

	// if this region is now completely unused, now's a good time to check for regions we can free
	if (is_free) {
		do_region_free();
	}

	return true;
};

ferr_t fmempool_allocate(size_t byte_count, size_t* out_allocated_byte_count, void** out_allocated_start) {
	void* allocated = NULL;

	if (byte_count == SIZE_MAX || out_allocated_start == NULL) {
		return ferr_invalid_argument;
	} else if (byte_count == 0) {
		if (out_allocated_byte_count) {
			*out_allocated_byte_count = 0;
		}
		*out_allocated_start = NO_BYTES_POINTER;
		return ferr_ok;
	}

	// first, try allocating an existing region
	allocated = allocate_existing(byte_count);

	if (!allocated) {
		// okay, now let's try allocating a new region
		allocated = allocate_new(byte_count);

		if (!allocated) {
			// still no luck? looks like the kernel ran out of memory! (that can't be good!)
			return ferr_temporary_outage;
		}
	}

	if (out_allocated_byte_count) {
		*out_allocated_byte_count = leaf_count_of_order(min_order_for_byte_count(byte_count)) * LEAF_SIZE;
	}

	*out_allocated_start = allocated;

	return ferr_ok;
};

ferr_t fmempool_reallocate(void* old_address, size_t new_byte_count, size_t* out_reallocated_byte_count, void** out_reallocated_start) {
	fmempool_region_header_t* old_parent_region = NULL;
	size_t old_order = 0;
	size_t new_order = min_order_for_byte_count(new_byte_count);
	void* new_address = NULL;

	if (new_byte_count == SIZE_MAX || out_reallocated_start == NULL || new_byte_count > MAX_ALLOCATION_SIZE) {
		return ferr_invalid_argument;
	} else if (old_address == NO_BYTES_POINTER || old_address == NULL) {
		return fmempool_allocate(new_byte_count, out_reallocated_byte_count, out_reallocated_start);
	} else if (new_byte_count == 0) {
		ferr_t status;

		status = fmempool_free(old_address);
		if (status != ferr_ok) {
			return status;
		}

		if (out_reallocated_byte_count) {
			*out_reallocated_byte_count = 0;
		}

		*out_reallocated_start = NO_BYTES_POINTER;

		return ferr_ok;
	}

	old_parent_region = find_parent_region(old_address);
	if (!old_parent_region) {
		// not previously allocated
		return ferr_invalid_argument;
	}

	// old parent region's lock is held here

	old_order = leaf_order(old_parent_region, old_address);

	if (new_order == old_order) {
		// great! we can just hand them back the same region.
		new_address = old_address;

		flock_spin_intsafe_unlock(&old_parent_region->lock);
	} else if (new_order < old_order) {
		// awesome, we're shrinking. we can always do this in-place.
		uintptr_t start_split = 0;

		// shrink the block
		set_leaf_order(old_parent_region, old_address, new_order);

		// and mark the other blocks as free
		start_split = (uintptr_t)old_address + (leaf_count_of_order(new_order) * LEAF_SIZE);
		for (size_t order = new_order; order < old_order; ++order) {
			fmempool_free_leaf_t* leaf = (void*)start_split;
			insert_free_leaf(old_parent_region, leaf, order);
			start_split += (leaf_count_of_order(order) * LEAF_SIZE);
		}

		new_address = old_address;

		flock_spin_intsafe_unlock(&old_parent_region->lock);
	} else {
		// okay, this is the hard case: we're expanding.
		bool can_expand_in_place = true;

		// first, check if we have buddies we can combine with to expand in-place
		for (size_t order = old_order; order < new_order; ++order) {
			fmempool_free_leaf_t* buddy = find_buddy(old_parent_region, old_address, order);

			// no buddy? whelp, we can't expand in-place.
			if (!buddy) {
				can_expand_in_place = false;
				break;
			}

			// if our buddy is behind us, we can't expand in-place.
			if ((void*)buddy < old_address) {
				can_expand_in_place = false;
				break;
			}

			// if our buddy is in-use, we can't expand in-place.
			if (leaf_is_in_use(old_parent_region, buddy)) {
				can_expand_in_place = false;
				break;
			}

			// if our buddy has been split, it's partially in-use; we can't expand in-place.
			if (leaf_order(old_parent_region, buddy) != order) {
				can_expand_in_place = false;
				break;
			}

			// okay, our buddy is usable!
		}

		if (can_expand_in_place) {
			// awesome, we can expand right here.

			// first, remove our buddies from the free list
			for (size_t order = old_order; order < new_order; ++order) {
				fmempool_free_leaf_t* buddy = find_buddy(old_parent_region, old_address, order);
				remove_free_leaf(old_parent_region, buddy, order);
			}

			// now expand ourselves
			set_leaf_order(old_parent_region, old_address, new_order);

			new_address = old_address;

			// we can now drop the lock
			flock_spin_intsafe_unlock(&old_parent_region->lock);
		} else {
			// alright, looks like we gotta allocate a new region
			ferr_t status;

			// we need to drop the old parent region lock
			flock_spin_intsafe_unlock(&old_parent_region->lock);

			// then allocate the new region
			status = fmempool_allocate(new_byte_count, NULL, &new_address);

			if (status != ferr_ok) {
				return status;
			}

			// next, copy the old data
			simple_memcpy(new_address, old_address, leaf_count_of_order(old_order) * LEAF_SIZE);

			// finally, free the old region
			if (fmempool_free(old_address) != ferr_ok) {
				// this literally can't fail, so just panic
				fpanic("failed to free old address during fmempool_reallocate");
			}
		}
	}

	if (out_reallocated_byte_count) {
		*out_reallocated_byte_count = leaf_count_of_order(new_order) * LEAF_SIZE;
	}
	*out_reallocated_start = new_address;

	return ferr_ok;
};

ferr_t fmempool_free(void* address) {
	if (address == NULL) {
		return ferr_invalid_argument;
	} else if (address == NO_BYTES_POINTER) {
		return ferr_ok;
	}

	if (!free_leaf(address)) {
		// it wasn't allocated
		return ferr_invalid_argument;
	}

	return ferr_ok;
};
