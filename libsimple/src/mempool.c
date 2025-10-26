/*
 * This file is part of Anillo OS
 * Copyright (C) 2022 Anillo OS Developers
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

#include <libsimple/mempool.h>
#include <libsimple/general.h>
#include <ferro/bits.h>

#include <stdint.h>
#include <stdbool.h>

#ifndef LIBSIMPLE_MEMPOOL_DEBUG
	#define LIBSIMPLE_MEMPOOL_DEBUG 0
#endif

// alright, so, each block here needs a byte for bookkeeping.
// the overhead of this bookkeeping is 6.25% of the total memory for a region.
// e.g. with a leaf size of 16 bytes, a region of 64KiB would require an additional 4KiB for bookkeeping.

FERRO_STRUCT(simple_mempool_free_leaf) {
	simple_mempool_free_leaf_t** prev;
	simple_mempool_free_leaf_t* next;
};

LIBSIMPLE_ALWAYS_INLINE bool address_is_canonical(void* address) {
	if ((uintptr_t)address & (1ull << 47)) {
		return ((uintptr_t)address >> 48) == 0xffff;
	} else {
		return ((uintptr_t)address >> 48) == 0;
	}
};

LIBSIMPLE_ALWAYS_INLINE size_t header_bookkeeping_count(simple_mempool_instance_t* instance) {
	return instance->options.page_size - sizeof(simple_mempool_region_header_t) - (sizeof(simple_mempool_free_leaf_t*) * instance->options.max_order);
};

LIBSIMPLE_ALWAYS_INLINE simple_mempool_free_leaf_t** region_buckets(simple_mempool_region_header_t* region) {
	return (void*)region->data;
};

LIBSIMPLE_ALWAYS_INLINE uint8_t* region_bookkeeping(simple_mempool_region_header_t* region) {
	return (void*)((char*)region->data + (sizeof(simple_mempool_free_leaf_t*) * region->instance->options.max_order));
};

LIBSIMPLE_ALWAYS_INLINE uint64_t simple_mempool_round_up_leaf(simple_mempool_instance_t* instance, uint64_t number) {
	return (number + instance->options.min_leaf_size - 1) & -instance->options.min_leaf_size;
};

LIBSIMPLE_ALWAYS_INLINE size_t leaf_count_of_order(size_t order) {
	return 1ULL << order;
};

LIBSIMPLE_ALWAYS_INLINE size_t size_of_order(simple_mempool_instance_t* instance, size_t order) {
	return leaf_count_of_order(order) * instance->options.min_leaf_size;
};

LIBSIMPLE_ALWAYS_INLINE size_t max_allocation_size(simple_mempool_instance_t* instance) {
	return leaf_count_of_order(instance->options.max_order) * instance->options.min_leaf_size;
};

LIBSIMPLE_ALWAYS_INLINE size_t min_order_for_leaf_count(simple_mempool_instance_t* instance, size_t leaf_count) {
	if (leaf_count == 0) {
		return SIZE_MAX;
	} else {
		size_t result = ferro_bits_in_use_u64(leaf_count) - 1;
		if (result >= instance->options.max_order) {
			return instance->options.max_order - 1;
		}
		return (leaf_count > leaf_count_of_order(result)) ? (result + 1) : result;
	}
};

LIBSIMPLE_ALWAYS_INLINE size_t max_order_of_leaf_count(simple_mempool_instance_t* instance, size_t leaf_count) {
	if (leaf_count == 0) {
		return SIZE_MAX;
	} else {
		size_t result = ferro_bits_in_use_u64(leaf_count) - 1;
		if (result >= instance->options.max_order) {
			return instance->options.max_order - 1;
		}
		return result;
	}
};

LIBSIMPLE_ALWAYS_INLINE size_t min_order_for_byte_count(simple_mempool_instance_t* instance, size_t byte_count) {
	return min_order_for_leaf_count(instance, simple_mempool_round_up_leaf(instance, byte_count) / instance->options.min_leaf_size);
};

/**
 * Returns the given leaf's index within bookkeeping array.
 */
LIBSIMPLE_ALWAYS_INLINE size_t leaf_index(const simple_mempool_region_header_t* parent_region, const simple_mempool_free_leaf_t* leaf) {
	return ((uintptr_t)leaf - (uintptr_t)parent_region->start) / parent_region->instance->options.min_leaf_size;
};

/**
 * Returns whether the given leaf is in-use or not.
 */
static bool leaf_is_in_use(simple_mempool_region_header_t* parent_region, const simple_mempool_free_leaf_t* leaf) {
	return (region_bookkeeping(parent_region)[leaf_index(parent_region, leaf)] & (1 << 7)) != 0;
};

/**
 * Sets whether the given leaf is in-use or not.
 */
static void set_leaf_is_in_use(simple_mempool_region_header_t* parent_region, const simple_mempool_free_leaf_t* leaf, bool is_in_use) {
	uint8_t* byte = &region_bookkeeping(parent_region)[leaf_index(parent_region, leaf)];
	if (is_in_use) {
		*byte |= 1 << 7;
	} else {
		*byte &= ~(1 << 7);
	}
};

/**
 * Returns the given leaf's order.
 */
static size_t leaf_order(simple_mempool_region_header_t* parent_region, const simple_mempool_free_leaf_t* leaf) {
	return region_bookkeeping(parent_region)[leaf_index(parent_region, leaf)] & 0x1f;
};

/**
 * Sets the given leaf's order.
 */
static void set_leaf_order(simple_mempool_region_header_t* parent_region, const simple_mempool_free_leaf_t* leaf, size_t order) {
	uint8_t* byte = &region_bookkeeping(parent_region)[leaf_index(parent_region, leaf)];
	*byte = (*byte & ~(0x1f)) | (order & 0x1f);
};

#if LIBSIMPLE_MEMPOOL_DEBUG
/**
 * Checks whether all the leaves in the region are valid.
 * Used for debugging.
 *
 * @note This a VERY expensive call (it's `O(n^2)`, with n being the number of leaves in the region).
 */
static void simple_mempool_region_check_leaves(simple_mempool_region_header_t* region) {
	for (size_t order = 0; order < region->instance->options.max_order; ++order) {
		size_t size = leaf_count_of_order(order) * region->instance->options.min_leaf_size;

		if (region_buckets(region)[order] != NULL && !address_is_canonical(region_buckets(region)[order])) {
			region->instance->allocator.panic("simple_mempool_region_check_leaves: Invalid leaf address (%p; source (bucket) = %p, order = %zu)", region_buckets(region)[order], &region_buckets(region)[order], order);
		}

		for (simple_mempool_free_leaf_t* leaf = region_buckets(region)[order]; leaf != NULL; leaf = leaf->next) {
			if (!address_is_canonical(leaf)) {
				region->instance->allocator.panic("simple_mempool_region_check_leaves: Invalid leaf address (%p)", leaf);
			}

			if (leaf->next != NULL && !address_is_canonical(leaf->next)) {
				region->instance->allocator.panic("simple_mempool_region_check_leaves: Invalid leaf address (%p; source (leaf) = %p)", leaf->next, &leaf->next);
			}

			if (!leaf->prev) {
				region->instance->allocator.panic("Invalid leaf (no prev value)");
			}

			void* leaf_start = leaf;
			void* leaf_end = (char*)leaf + size;

			// check that it doesn't overlap with any free leaves
			for (size_t order2 = 0; order2 < region->instance->options.max_order; ++order2) {
				size_t size2 = leaf_count_of_order(order2) * region->instance->options.min_leaf_size;

				if (region_buckets(region)[order2] != NULL && !address_is_canonical(region_buckets(region)[order2])) {
					region->instance->allocator.panic("simple_mempool_region_check_leaves: Invalid leaf address (%p; source (bucket) = %p, order = %zu)", region_buckets(region)[order2], &region_buckets(region)[order2], order);
				}

				for (simple_mempool_free_leaf_t* leaf2 = region_buckets(region)[order2]; leaf2 != NULL; leaf2 = leaf2->next) {
					if (!address_is_canonical(leaf2)) {
						region->instance->allocator.panic("simple_mempool_region_check_leaves: Invalid leaf address (%p)", leaf2);
					}

					if (leaf2->next != NULL && !address_is_canonical(leaf2->next)) {
						region->instance->allocator.panic("simple_mempool_region_check_leaves: Invalid leaf address (%p; source (leaf) = %p)", leaf2->next, &leaf2->next);
					}

					if (leaf == leaf2) {
						continue;
					}

					if (!leaf2->prev) {
						region->instance->allocator.panic("Invalid leaf (no prev value)");
					}

					void* leaf2_start = leaf2;
					void* leaf2_end = (char*)leaf2 + size2;

					if (
						(leaf_start <= leaf2_start && leaf_end > leaf2_start) ||
						(leaf2_start <= leaf_start && leaf2_end > leaf_start)
					) {
						region->instance->allocator.panic("Overlapping leaves");
					}
				}
			}

			// now check that it doesn't overlap with any used leaves
			for (size_t i = 0; i < leaf_count_of_order(order); ++i) {
				if ((region_bookkeeping(region)[leaf_index(region, leaf) + i] & (1 << 7)) != 0) {
					region->instance->allocator.panic("Free leaf has in-use subleaves");
				}
			}
		}
	}
};
#endif

/**
 * Inserts the given leaf into the appropriate bucket in the parent region.
 */
static void insert_free_leaf(simple_mempool_region_header_t* parent_region, simple_mempool_free_leaf_t* leaf, size_t order) {
#if LIBSIMPLE_MEMPOOL_DEBUG
	if (!address_is_canonical(leaf)) {
		parent_region->instance->allocator.panic("insert_free_leaf: Invalid leaf address (%p)", leaf);
	}
#endif

	leaf->prev = &region_buckets(parent_region)[order];
	leaf->next = region_buckets(parent_region)[order];

	if (leaf->next) {
		if (parent_region->instance->allocator.unpoison) {
			parent_region->instance->allocator.unpoison((uintptr_t)&leaf->next->prev, sizeof(simple_mempool_free_leaf_t*));
		}

		leaf->next->prev = &leaf->next;

		if (parent_region->instance->allocator.poison) {
			parent_region->instance->allocator.poison((uintptr_t)&leaf->next->prev, sizeof(simple_mempool_free_leaf_t*));
		}
	}

	region_buckets(parent_region)[order] = leaf;

	set_leaf_order(parent_region, leaf, order);

	parent_region->free_count += leaf_count_of_order(order);

	set_leaf_is_in_use(parent_region, leaf, false);

#if LIBSIMPLE_MEMPOOL_DEBUG
	simple_mempool_region_check_leaves(parent_region);
#endif

	if (parent_region->instance->allocator.poison) {
		parent_region->instance->allocator.poison((uintptr_t)leaf, size_of_order(parent_region->instance, order));
	}
};

/**
 * Removes the given leaf from the appropriate bucket in the parent region.
 *
 * @note This does NOT take care of setting the leaf as in-use.
 */
static void remove_free_leaf(simple_mempool_region_header_t* parent_region, simple_mempool_free_leaf_t* leaf, size_t order) {
	if (parent_region->instance->allocator.unpoison) {
		parent_region->instance->allocator.unpoison((uintptr_t)leaf, size_of_order(parent_region->instance, order));
	}

#if LIBSIMPLE_MEMPOOL_DEBUG
	if (!leaf->prev) {
		parent_region->instance->allocator.panic("invalid leaf");
	}
#endif

	if (parent_region->instance->allocator.unpoison && leaf->prev != &region_buckets(parent_region)[order]) {
		parent_region->instance->allocator.unpoison((uintptr_t)leaf->prev, sizeof(simple_mempool_free_leaf_t*));
	}

	*leaf->prev = leaf->next;

	if (parent_region->instance->allocator.poison && leaf->prev != &region_buckets(parent_region)[order]) {
		parent_region->instance->allocator.poison((uintptr_t)leaf->prev, sizeof(simple_mempool_free_leaf_t*));
	}

	if (leaf->next) {
		if (parent_region->instance->allocator.unpoison) {
			parent_region->instance->allocator.unpoison((uintptr_t)&leaf->next->prev, sizeof(simple_mempool_free_leaf_t*));
		}

		leaf->next->prev = leaf->prev;

		if (parent_region->instance->allocator.poison) {
			parent_region->instance->allocator.poison((uintptr_t)&leaf->next->prev, sizeof(simple_mempool_free_leaf_t*));
		}
	}

	parent_region->free_count -= leaf_count_of_order(order);

#if LIBSIMPLE_MEMPOOL_DEBUG
	simple_mempool_region_check_leaves(parent_region);
#endif
};

/**
 * Finds the given leaf's buddy.
 */
static simple_mempool_free_leaf_t* find_buddy(simple_mempool_region_header_t* parent_region, simple_mempool_free_leaf_t* leaf, size_t order) {
	uintptr_t parent_start = (uintptr_t)parent_region->start;
	size_t leaf_count = leaf_count_of_order(order);
	uintptr_t maybe_buddy = (((uintptr_t)leaf - parent_start) ^ (leaf_count * parent_region->instance->options.min_leaf_size)) + parent_start;

	if (maybe_buddy + (leaf_count * parent_region->instance->options.min_leaf_size) > parent_start + (parent_region->leaf_count * parent_region->instance->options.min_leaf_size)) {
		return NULL;
	}

	return (simple_mempool_free_leaf_t*)maybe_buddy;
};

FERRO_ALWAYS_INLINE uint64_t simple_mempool_round_up_page(simple_mempool_instance_t* instance, uint64_t number) {
	return (number + instance->options.page_size - 1) & -instance->options.page_size;
};

FERRO_ALWAYS_INLINE uint64_t simple_mempool_round_up_to_page_count(simple_mempool_instance_t* instance, uint64_t byte_count) {
	return simple_mempool_round_up_page(instance, byte_count) / instance->options.page_size;
};

FERRO_ALWAYS_INLINE uintptr_t simple_mempool_region_boundary(uintptr_t start, size_t length, uint8_t boundary_alignment_power) {
	if (boundary_alignment_power > 63) {
		return 0;
	}
	uintptr_t boundary_alignment_mask = (1ull << boundary_alignment_power) - 1;
	uintptr_t next_boundary = (start & ~boundary_alignment_mask) + (1ull << boundary_alignment_power);
	return (next_boundary > start && next_boundary < start + length) ? next_boundary : 0;
};

/**
 * Returns the necessary region size for the given leaf count.
 *
 * Note that this is *only* the region size, i.e. the header space is not included.
 */
LIBSIMPLE_ALWAYS_INLINE
size_t region_size_for_leaf_count(simple_mempool_instance_t* instance, size_t leaf_count) {
	return simple_mempool_round_up_page(instance, leaf_count * instance->options.min_leaf_size);
};

/**
 * Returns the necessary header size for the given leaf count, including bookkeeping info space.
 *
 * Note that this is *only* the header size, i.e. the region space is not included.
 */
static size_t header_size_for_leaf_count(simple_mempool_instance_t* instance, size_t leaf_count, size_t* out_extra_bookkeeping_page_count) {
	size_t region_size = instance->options.page_size;
	size_t extra_bookkeeping_page_count = 0;

	if (leaf_count > header_bookkeeping_count(instance)) {
		extra_bookkeeping_page_count = simple_mempool_round_up_to_page_count(instance, leaf_count - header_bookkeeping_count(instance));
		region_size += extra_bookkeeping_page_count * instance->options.page_size;
	}

	if (out_extra_bookkeeping_page_count) {
		*out_extra_bookkeeping_page_count = extra_bookkeeping_page_count;
	}

	return region_size;
};

/**
 * Returns a pointer to the previous region of the given region.
 *
 * Care must be taken to ensure the given region is NOT the first region in the list.
 */
LIBSIMPLE_ALWAYS_INLINE simple_mempool_region_header_t* region_from_prev(simple_mempool_region_header_t* region) {
	return (simple_mempool_region_header_t*)((uintptr_t)region->prev - offsetof(simple_mempool_region_header_t, next));
};

static void remove_region(simple_mempool_region_header_t* region, simple_mempool_region_header_t** head) {
	*region->prev = region->next;
	if (region->next) {
		region->next->prev = region->prev;
	}
};

static void insert_region(simple_mempool_region_header_t* region, simple_mempool_region_header_t** head) {
	simple_mempool_region_header_t* old_first;

	old_first = *head;

	if (old_first) {
		old_first->prev = &region->next;
	}

	region->prev = head;
	region->next = old_first;
	*head = region;
};

/**
 * Iterates through the list of regions and frees unnecessary regions.
 *
 * This function tries to keep the `n` largest regions, where `n` is the number of regions to keep,
 * as set in the options of this instance.
 */
static void do_region_free(simple_mempool_instance_t* instance) {
	// this is a VLA (!)
	simple_mempool_region_header_t* kept[instance->options.max_kept_region_count];
	simple_mempool_region_header_t* free_these = NULL;

	simple_memset(kept, 0, sizeof(*kept) * instance->options.max_kept_region_count);

	// first, find the free regions
	for (simple_mempool_region_header_t* region = instance->regions_head; region != NULL; /* handled in the body */) {
		simple_mempool_region_header_t* next_region;
		bool kept_it = false;

		// not completely unused; skip it
		if (region->free_count != region->leaf_count) {
			region = region->next;
			continue;
		}

		// no matter what we do next, we're going to remove this region from the region list
		remove_region(region, &instance->regions_head);

		next_region = region->next;

		for (size_t i = 0; i < instance->options.max_kept_region_count; ++i) {
			if (!kept[i]) {
				kept[i] = region;
				kept_it = true;
				break;
			}

			if (kept[i]->leaf_count < region->leaf_count) {
				// insert the previously kept region into the free list
				insert_region(kept[i], &free_these);
				kept[i] = region;
				kept_it = true;
				break;
			}
		}

		// if we're not going to keep it, add it to the list of regions to free
		if (!kept_it) {
			insert_region(region, &free_these);
		}

		region = next_region;
	}

	// add the regions we decided to keep back into the region list
	for (size_t i = 0; i < instance->options.max_kept_region_count; ++i) {
		if (!kept[i]) {
			continue;
		}
		insert_region(kept[i], &instance->regions_head);
	}

	// now free the others
	for (simple_mempool_region_header_t* region = free_these; region != NULL; /* handled in the body */) {
		simple_mempool_region_header_t* next_region = region->next;
		size_t region_page_count = simple_mempool_round_up_to_page_count(instance, region_size_for_leaf_count(instance, region->leaf_count));

		// free it
		// ignore errors

		instance->allocator.free(instance->context, region_page_count, region->start);
		instance->allocator.free_header(instance->context, simple_mempool_round_up_to_page_count(instance, header_size_for_leaf_count(instance, region->leaf_count, NULL)), region);

		region = next_region;
	}
};

/**
 * Attempts to fulfill the given allocation using an existing region already present in the region list.
 */
static void* allocate_existing(simple_mempool_instance_t* instance, size_t byte_count, uint8_t alignment_power, uint8_t boundary_alignment_power) {
	if (alignment_power < instance->options.min_leaf_alignment) {
		alignment_power = instance->options.min_leaf_alignment;
	}

	uintptr_t alignment_mask = ((uintptr_t)1 << alignment_power) - 1;
	size_t min_order = min_order_for_byte_count(instance, byte_count);

	simple_mempool_region_header_t* candidate_parent_region = NULL;
	simple_mempool_free_leaf_t* candidate_leaf = NULL;
	size_t candidate_order = instance->options.max_order;
	uintptr_t start_split = 0;

	simple_mempool_free_leaf_t* aligned_candidate_leaf = NULL;
	size_t aligned_candidate_order = instance->options.max_order;

	// first, look for the smallest usable block from any region
	// XXX: `flags` is simple_mempool_flags_t but acquire_first_region() expects simple_mempool_region_flags_t.
	//      they're currently identical, but this may change; be aware of this in the future.
	for (simple_mempool_region_header_t* region = instance->regions_head; region != NULL; region = region->next) {
		fassert(region->instance == instance);

		for (size_t order = min_order; order < instance->options.max_order && order < candidate_order; ++order) {
			simple_mempool_free_leaf_t* leaf = region_buckets(region)[order];

			if (!leaf) {
				continue;
			}

			if (((uintptr_t)leaf & alignment_mask) != 0) {
				if (order > min_order) {
					// the start of this leaf isn't aligned the way we want;
					// let's see if a subleaf within it is...
					uintptr_t next_aligned_address = ((uintptr_t)leaf & ~alignment_mask) + (alignment_mask + 1);

					if (next_aligned_address > (uintptr_t)leaf && next_aligned_address < (uintptr_t)leaf + size_of_order(instance, order)) {
						// okay great, the next aligned address falls within this leaf.
						// however, let's see if the subleaf is big enough for us.
						uintptr_t leaf_end = (uintptr_t)leaf + size_of_order(instance, order);
						uintptr_t subleaf = (uintptr_t)leaf;
						size_t suborder = order - 1;
						bool found = false;

						while (suborder >= min_order && subleaf < leaf_end) {
							if (((uintptr_t)subleaf & alignment_mask) != 0) {
								// awesome, this subleaf is big enough and it's aligned properly
								found = true;
								aligned_candidate_leaf = (void*)subleaf;
								aligned_candidate_order = suborder;
								break;
							} else {
								if (next_aligned_address > (uintptr_t)subleaf && next_aligned_address < (uintptr_t)subleaf + size_of_order(instance, suborder)) {
									// okay, so this subleaf contains the address; let's search its subleaves
									if (suborder == min_order) {
										// can't split up a min order leaf to get an aligned leaf big enough
										break;
									} else {
										leaf_end = size_of_order(instance, suborder);
										--suborder;
									}
								} else {
									// nope, this subleaf doesn't contain the address; let's skip it
									subleaf += size_of_order(instance, suborder);
								}
							}
						}

						if (!found) {
							// none of this leaf's subleaves were big enough and aligned properly
							continue;
						}

						// great, we have an aligned subleaf big enough; let's go ahead and save this candidate
					} else {
						// nope, the next aligned address isn't in this leaf.
						continue;
					}
				} else {
					// can't split up a min order leaf to get an aligned leaf big enough
					continue;
				}
			} else {
				aligned_candidate_leaf = leaf;
				aligned_candidate_order = order;
			}

			// make sure the region doesn't cross a boundary we don't want to cross
			//
			// note that we perform this check using the aligned candidate leaf, since this is the actual
			// starting address of the region we will be handing back to the caller (even when no alignment was requested).
			if (simple_mempool_region_boundary((uintptr_t)aligned_candidate_leaf, byte_count, boundary_alignment_power) != 0) {
				continue;
			}

			// allow the allocator to perform some addition alignment checking of its own
			// (e.g. checking the physical alignment for the backing physical memory)
			if (instance->allocator.is_aligned) {
				if (!instance->allocator.is_aligned(instance->context, aligned_candidate_leaf, byte_count, alignment_power, boundary_alignment_power)) {
					continue;
				}
			}

			candidate_order = order;
			candidate_leaf = leaf;
			candidate_parent_region = region;
			break;
		}

		if (candidate_order == min_order) {
			break;
		}
	}

	// no free blocks big enough in any region
	if (!candidate_leaf) {
		return NULL;
	}

	remove_free_leaf(candidate_parent_region, candidate_leaf, candidate_order);

	if (((uintptr_t)candidate_leaf & alignment_mask) != 0) {
		// alright, if we have an unaligned candidate leaf, we've already determined that
		// it does have an aligned subleaf big enough for us, so let's split up the leaf to get it.

		uintptr_t leaf_end = (uintptr_t)candidate_leaf + size_of_order(instance, candidate_order);
		uintptr_t subleaf = (uintptr_t)candidate_leaf;
		size_t suborder = candidate_order - 1;

		while (suborder >= aligned_candidate_order) {
			uintptr_t next_subleaf = 0;

			for (uintptr_t split_leaf = subleaf; split_leaf < leaf_end; split_leaf += size_of_order(instance, suborder)) {
				if ((uintptr_t)aligned_candidate_leaf >= (uintptr_t)subleaf && (uintptr_t)aligned_candidate_leaf < (uintptr_t)subleaf + size_of_order(instance, suborder)) {
					// this leaf either is the aligned candidate leaf or contains the aligned candidate leaf
					next_subleaf = split_leaf;
				} else {
					// this is just a leaf we don't care about; add it back to the region
					insert_free_leaf(candidate_parent_region, (void*)split_leaf, suborder);
				}
			}

			if (suborder == aligned_candidate_order) {
				// this is the order of the aligned candidate leaf, so this next subleaf MUST be the aligned candidate leaf
				fassert(next_subleaf == (uintptr_t)aligned_candidate_leaf);
				candidate_leaf = aligned_candidate_leaf;
				candidate_order = aligned_candidate_order;
				break;
			} else {
				// this is NOT the order of the aligned candidate leaf, so this MUST NOT be the aligned candidate leaf
				fassert(next_subleaf != (uintptr_t)aligned_candidate_leaf);

				// now let's iterate through this leaf's subleaves
				subleaf = next_subleaf;
				leaf_end = subleaf + size_of_order(instance, suborder);
				--suborder;
			}
		}

		// the candidate leaf is now the aligned candidate leaf.
		// however, the aligned candidate leaf may have been too big for us,
		// so let's continue on with the usual shrinking/splitting case.
	}

	// we might have gotten a bigger block than we wanted. split it up.
	// to understand how this works, see allocate_frame() in `paging.c`.
	start_split = (uintptr_t)candidate_leaf + (leaf_count_of_order(min_order) * instance->options.min_leaf_size);
	for (size_t order = min_order; order < candidate_order; ++order) {
		simple_mempool_free_leaf_t* leaf = (void*)start_split;
		insert_free_leaf(candidate_parent_region, leaf, order);
		start_split += (leaf_count_of_order(order) * instance->options.min_leaf_size);
	}

	set_leaf_order(candidate_parent_region, candidate_leaf, min_order);
	set_leaf_is_in_use(candidate_parent_region, candidate_leaf, true);

	return candidate_leaf;
};

/**
 * Allocates a brand new region for the given allocation and inserts it into the region list.
 */
static void* allocate_new(simple_mempool_instance_t* instance, size_t byte_count, uint8_t alignment_power, uint8_t boundary_alignment_power) {
	size_t min_order = min_order_for_byte_count(instance, byte_count);
	size_t region_order = (min_order < instance->options.optimal_min_region_order) ? instance->options.optimal_min_region_order : min_order;
	simple_mempool_region_header_t* header = NULL;
	size_t header_size = 0;
	size_t region_size = 0;
	size_t extra_bookkeeping_page_count = 0;
	size_t leaves_allocated = 0;
	size_t leaf_count = 0;
	void* physical_region_start = NULL;
	void* region_start = NULL;
	size_t region_page_count = 0;

	while (region_order > instance->options.max_order) {
		region_order /= 2;
	}

	do {
		leaf_count = leaf_count_of_order(region_order);
		header_size = header_size_for_leaf_count(instance, leaf_count, &extra_bookkeeping_page_count);
		region_size = region_size_for_leaf_count(instance, leaf_count);
		region_page_count = simple_mempool_round_up_to_page_count(instance, region_size);

		if (instance->allocator.allocate(instance->context, region_page_count, alignment_power, boundary_alignment_power, &region_start) != ferr_ok) {
			goto shrink_order;
		}

		if (instance->allocator.allocate_header(instance->context, simple_mempool_round_up_to_page_count(instance, header_size), (void*)&header) != ferr_ok) {
			instance->allocator.free(instance->context, region_page_count, region_start);
			goto shrink_order;
		}

		// region successfully allocated
		break;

shrink_order:
		// can't go lower our minimum order
		if (region_order == min_order) {
			return NULL;
		}

		// otherwise, try half of our previous order
		region_order /= 2;
	} while (1);

	insert_region(header, &instance->regions_head);

	header->leaf_count = leaf_count;
	header->free_count = 0;
	header->start = region_start;
	header->instance = instance;

	simple_memset(&region_bookkeeping(header)[0], 0, header_bookkeeping_count(instance));
	for (size_t i = 0; i < extra_bookkeeping_page_count; ++i) {
		uint8_t* page = (uint8_t*)header + instance->options.page_size + (i * instance->options.page_size);
		simple_memset(page, 0, instance->options.page_size);
	}

	simple_memset(&region_buckets(header)[0], 0, sizeof(simple_mempool_free_leaf_t*) * instance->options.max_order);

	while (leaves_allocated < leaf_count) {
		size_t order = max_order_of_leaf_count(instance, leaf_count - leaves_allocated);
		size_t leaves = leaf_count_of_order(order);
		void* addr = (void*)((uintptr_t)header->start + (leaves_allocated * instance->options.min_leaf_size));

		insert_free_leaf(header, addr, order);

		leaves_allocated += leaves;
	}

	// alright, this has to succeed now
	return allocate_existing(instance, byte_count, alignment_power, boundary_alignment_power);
};

/**
 * Returns `true` if the given leaf belongs to the given region.
 */
LIBSIMPLE_ALWAYS_INLINE bool leaf_belongs_to_region(simple_mempool_free_leaf_t* leaf, simple_mempool_region_header_t* region) {
	return (uintptr_t)leaf >= (uintptr_t)region->start && (uintptr_t)leaf < (uintptr_t)region->start + (region->leaf_count * region->instance->options.min_leaf_size);
};

static simple_mempool_region_header_t* find_parent_region(simple_mempool_instance_t* instance, simple_mempool_free_leaf_t* leaf) {
	simple_mempool_region_header_t* parent_region = NULL;

#if LIBSIMPLE_MEMPOOL_DEBUG
	// make sure the leaf is aligned to the minimum leaf alignment
	// (it's not a valid leaf if it's not aligned properly)
	if ((uintptr_t)leaf & ((1ull << instance->options.min_leaf_alignment) - 1)) {
		instance->allocator.panic("Invalid (unaligned) leaf");
	}
#endif

	for (simple_mempool_region_header_t* region = instance->regions_head; region != NULL; region = region->next) {
		fassert(region->instance == instance);

		if (leaf_belongs_to_region(leaf, region)) {
			parent_region = region;
			break;
		}
	}

	return parent_region;
};

static bool free_leaf(simple_mempool_instance_t* instance, void* address) {
	size_t order = 0;
	simple_mempool_free_leaf_t* leaf = address;
	simple_mempool_region_header_t* parent_region = find_parent_region(instance, leaf);
	bool is_free = false;

	if (!parent_region) {
		return false;
	}

	order = leaf_order(parent_region, leaf);

	if (!leaf_is_in_use(parent_region, leaf)) {
		instance->allocator.panic("Freeing unused leaf");
	}

	set_leaf_is_in_use(parent_region, leaf, false);

	// find buddies to merge with
	for (; order < instance->options.max_order; ++order) {
		simple_mempool_free_leaf_t* buddy = find_buddy(parent_region, leaf, order);

		// oh, no buddy? how sad :(
		if (!buddy) {
			break;
		}

		if (leaf_is_in_use(parent_region, buddy)) {
			// whelp, our buddy is in use. we can't do any more merging
			break;
		}

		if (leaf_order(parent_region, buddy) != order) {
			// if our buddy has been split, it's partially in-use. we can't merge with it.
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

	// if this region is now completely unused, now's a good time to check for regions we can free
	if (is_free) {
		do_region_free(instance);
	}

	return true;
};

ferr_t simple_mempool_allocate(simple_mempool_instance_t* instance, size_t byte_count, uint8_t alignment_power, uint8_t boundary_alignment_power, size_t* out_allocated_byte_count, void** out_allocated_start) {
	void* allocated = NULL;

	if (byte_count == SIZE_MAX || out_allocated_start == NULL) {
		return ferr_invalid_argument;
	} else if (byte_count == 0) {
		if (out_allocated_byte_count) {
			*out_allocated_byte_count = 0;
		}
		*out_allocated_start = LIBSIMPLE_MEMPOOL_NO_BYTES_POINTER;
		return ferr_ok;
	}

	// first, try allocating an existing region
	allocated = allocate_existing(instance, byte_count, alignment_power, boundary_alignment_power);

	if (!allocated) {
		// okay, now let's try allocating a new region
		allocated = allocate_new(instance, byte_count, alignment_power, boundary_alignment_power);

		if (!allocated) {
			// still no luck? looks like the kernel ran out of memory! (that can't be good!)
			return ferr_temporary_outage;
		}
	}

	if (out_allocated_byte_count) {
		*out_allocated_byte_count = leaf_count_of_order(min_order_for_byte_count(instance, byte_count)) * instance->options.min_leaf_size;
	}

	*out_allocated_start = allocated;

	return ferr_ok;
};

static ferr_t simple_mempool_reallocate_slow(simple_mempool_region_header_t* old_parent_region, void* old_address, size_t old_order, size_t new_byte_count, void** new_address, uint8_t alignment_power, uint8_t boundary_alignment_power) {
	// alright, looks like we gotta allocate a new region
	ferr_t status = ferr_ok;

	// allocate the new region
	status = simple_mempool_allocate(old_parent_region->instance, new_byte_count, alignment_power, boundary_alignment_power, NULL, new_address);

	if (status != ferr_ok) {
		return status;
	}

	// next, copy the old data
	simple_memcpy(*new_address, old_address, leaf_count_of_order(old_order) * old_parent_region->instance->options.min_leaf_size);

	// finally, free the old region
	if (simple_mempool_free(old_parent_region->instance, old_address) != ferr_ok) {
		// this literally can't fail
		old_parent_region->instance->allocator.panic("Failed to free old address during simple_mempool_reallocate");
	}

	return status;
};

ferr_t simple_mempool_reallocate(simple_mempool_instance_t* instance, void* old_address, size_t new_byte_count, uint8_t alignment_power, uint8_t boundary_alignment_power, size_t* out_reallocated_byte_count, void** out_reallocated_start) {
	simple_mempool_region_header_t* old_parent_region = NULL;
	size_t old_order = 0;
	size_t new_order = min_order_for_byte_count(instance, new_byte_count);
	void* new_address = NULL;
	ferr_t status = ferr_ok;

	if (alignment_power < instance->options.min_leaf_alignment) {
		alignment_power = instance->options.min_leaf_alignment;
	}

	uintptr_t alignment_mask = ((uintptr_t)1 << alignment_power) - 1;

	if (new_byte_count == SIZE_MAX || out_reallocated_start == NULL || new_byte_count > max_allocation_size(instance)) {
		return ferr_invalid_argument;
	} else if (old_address == LIBSIMPLE_MEMPOOL_NO_BYTES_POINTER || old_address == NULL) {
		return simple_mempool_allocate(instance, new_byte_count, alignment_power, boundary_alignment_power, out_reallocated_byte_count, out_reallocated_start);
	} else if (new_byte_count == 0) {
		status = simple_mempool_free(instance, old_address);
		if (status != ferr_ok) {
			return status;
		}

		if (out_reallocated_byte_count) {
			*out_reallocated_byte_count = 0;
		}

		*out_reallocated_start = LIBSIMPLE_MEMPOOL_NO_BYTES_POINTER;

		return status;
	}

	old_parent_region = find_parent_region(instance, old_address);
	if (!old_parent_region) {
		// not previously allocated
		return ferr_invalid_argument;
	}

	old_order = leaf_order(old_parent_region, old_address);

	if (
		// if the current address doesn't have the required alignment,
		// it doesn't matter if we're shrinking or if we have room to expand place.
		// we *have* to go the slow route of allocating, copying, and freeing.
		((uintptr_t)old_address & alignment_mask) != 0 ||

		// if the region with the new size starting at the current address would cross a boundary, we have to use the slow path
		simple_mempool_region_boundary((uintptr_t)old_address, new_byte_count, boundary_alignment_power) != 0
	) {
		status = simple_mempool_reallocate_slow(old_parent_region, old_address, old_order, new_byte_count, &new_address, alignment_power, boundary_alignment_power);
	} else if (new_order == old_order) {
		// great! we can just hand them back the same region.
		new_address = old_address;
	} else if (new_order < old_order) {
		// awesome, we're shrinking. we can always do this in-place.
		uintptr_t start_split = 0;

		// shrink the block
		set_leaf_order(old_parent_region, old_address, new_order);

		// and mark the other blocks as free
		start_split = (uintptr_t)old_address + (leaf_count_of_order(new_order) * instance->options.min_leaf_size);
		for (size_t order = new_order; order < old_order; ++order) {
			simple_mempool_free_leaf_t* leaf = (void*)start_split;
			insert_free_leaf(old_parent_region, leaf, order);
			start_split += (leaf_count_of_order(order) * instance->options.min_leaf_size);
		}

		new_address = old_address;
	} else {
		// okay, this is the hard case: we're expanding.
		bool can_expand_in_place = true;

		// first, check if we have buddies we can combine with to expand in-place
		for (size_t order = old_order; order < new_order; ++order) {
			simple_mempool_free_leaf_t* buddy = find_buddy(old_parent_region, old_address, order);

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
				simple_mempool_free_leaf_t* buddy = find_buddy(old_parent_region, old_address, order);
				remove_free_leaf(old_parent_region, buddy, order);
			}

			// now expand ourselves
			set_leaf_order(old_parent_region, old_address, new_order);

			new_address = old_address;
		} else {
			// alright, looks like we gotta allocate a new region
			status = simple_mempool_reallocate_slow(old_parent_region, old_address, old_order, new_byte_count, &new_address, alignment_power, boundary_alignment_power);
		}
	}

	if (out_reallocated_byte_count) {
		*out_reallocated_byte_count = leaf_count_of_order(new_order) * instance->options.min_leaf_size;
	}
	*out_reallocated_start = new_address;

	return status;
};

ferr_t simple_mempool_free(simple_mempool_instance_t* instance, void* address) {
	if (address == NULL) {
		return ferr_invalid_argument;
	} else if (address == LIBSIMPLE_MEMPOOL_NO_BYTES_POINTER) {
		return ferr_ok;
	}

	if (!free_leaf(instance, address)) {
		// it wasn't allocated
		return ferr_invalid_argument;
	}

	return ferr_ok;
};

ferr_t simple_mempool_instance_init(simple_mempool_instance_t* instance, void* context, const simple_mempool_allocator_t* allocator, const simple_mempool_instance_options_t* options) {
	instance->context = context;
	simple_memcpy(&instance->allocator, allocator, sizeof(instance->allocator));
	simple_memcpy(&instance->options, options, sizeof(instance->options));
	instance->regions_head = NULL;
	return ferr_ok;
};

ferr_t simple_mempool_instance_destroy(simple_mempool_instance_t* instance) {
	simple_mempool_region_header_t* next = NULL;

	for (simple_mempool_region_header_t* region = instance->regions_head; region != NULL; region = next) {
		next = region->next;

		size_t region_page_count = simple_mempool_round_up_to_page_count(instance, region_size_for_leaf_count(instance, region->leaf_count));

		// free it
		// ignore errors

		instance->allocator.free(instance->context, region_page_count, region->start);
		instance->allocator.free_header(instance->context, simple_mempool_round_up_to_page_count(instance, header_size_for_leaf_count(instance, region->leaf_count, NULL)), region);
	}

	instance->regions_head = NULL;

	return ferr_ok;
};

bool simple_mempool_get_allocated_byte_count(simple_mempool_instance_t* instance, void* address, size_t* out_allocated_byte_count) {
	simple_mempool_region_header_t* parent_region = find_parent_region(instance, address);

	if (!parent_region) {
		// not allocated
		return false;
	}

	if (out_allocated_byte_count) {
		size_t order = leaf_order(parent_region, address);
		*out_allocated_byte_count = leaf_count_of_order(order) * parent_region->instance->options.min_leaf_size;
	}

	return true;
};
