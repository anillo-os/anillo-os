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

//! Common code for both physical and virtual memory region management.

use core::mem::MaybeUninit;

use intrusive_collections::{
	intrusive_adapter, linked_list::LinkedListOps, Adapter, LinkedList, LinkedListAtomicLink,
	PointerOps,
};

use super::{order::MAX_ORDER, PAGE_SIZE};
use crate::{memory::order::byte_count_of_order, util::align_down_pow2};

pub(super) struct FreeBlock {
	pub(super) link: LinkedListAtomicLink,
	pub(super) addr: u64,
}

intrusive_adapter!(pub(super) FreeBlockAdapter = &'static FreeBlock: FreeBlock { link: LinkedListAtomicLink });

pub(super) trait BuddyRegionHeaderWrapper {
	type Wrapped: BuddyRegionHeader;

	/// # Safety
	/// This must only be called when you can be certain that you are holding the only shared reference to `self`.
	///
	/// In practice, this trait should only be implemented on wrapper structs for structs that implement BuddyRegionHeader.
	/// Such structs should be used as members of a linked list which is used to keep track of all available memory regions.
	/// Having exclusive access to this list would mean that is safe to call this method on its members.
	unsafe fn inner(&self) -> &mut Self::Wrapped;
}

pub(super) struct CandidateBlockResult {
	order: usize,
	aligned_address: Option<u64>,
	aligned_order: Option<usize>,
}

impl CandidateBlockResult {
	pub fn order(&self) -> usize {
		self.order
	}

	pub fn aligned_address(&self) -> Option<u64> {
		self.aligned_address
	}

	pub fn aligned_order(&self) -> Option<usize> {
		self.aligned_order
	}
}

pub(super) trait BuddyRegionHeader {
	fn bitmap(&self) -> &[u8];
	fn bitmap_mut(&mut self) -> &mut [u8];
	fn start_address(&self) -> u64;
	fn page_count(&self) -> u64;
	fn buckets(&self) -> &[LinkedList<FreeBlockAdapter>; MAX_ORDER];
	fn buckets_mut(&mut self) -> &mut [LinkedList<FreeBlockAdapter>; MAX_ORDER];

	fn after_insert(&mut self, _block_addr: u64, _order: usize) {}
	fn after_remove(&mut self, _block: *mut FreeBlock, _order: usize) {}
	fn after_allocate_remove(_order: usize) {}
	fn after_free_buddy_remove(_order: usize) {}

	unsafe fn addr_to_insert_ptr(block_addr: u64) -> *mut MaybeUninit<FreeBlock> {
		block_addr as *mut MaybeUninit<FreeBlock>
	}

	fn bitmap_entry_for_block(&self, block_offset: u64) -> (&u8, u8) {
		let bitmap_index = block_offset / PAGE_SIZE;
		let byte_index = bitmap_index / 8;
		let bit_index = bitmap_index % 8;

		(&self.bitmap()[byte_index as usize], bit_index as u8)
	}

	fn bitmap_entry_for_block_mut(&mut self, block_offset: u64) -> (&mut u8, u8) {
		let bitmap_index = block_offset / PAGE_SIZE;
		let byte_index = bitmap_index / 8;
		let bit_index = bitmap_index % 8;

		(&mut self.bitmap_mut()[byte_index as usize], bit_index as u8)
	}

	fn perform_basic_checks(&self, block_addr: u64, order: usize) {
		// as basic (and cheap) sanity checks, ensure:
		//   * the block pointer lies within our usable region
		//   * the block pointer is aligned to the block size for the given order
		assert!(
			block_addr >= self.start_address()
				&& block_addr < self.start_address() + self.page_count() * PAGE_SIZE
		);
		let offset = block_addr - self.start_address();
		assert_eq!(
			offset - align_down_pow2(offset, byte_count_of_order(order)),
			0
		);
	}

	fn block_is_in_use(&self, block_addr: u64) -> bool {
		// we don't care about the order here, just make sure it's at least a valid order-0 block.
		self.perform_basic_checks(block_addr, 0);
		let (byte, bit) = self.bitmap_entry_for_block(block_addr - self.start_address());
		((*byte) & (1u8 << bit)) != 0
	}

	// NOTE: this private fn assumes that basic sanity checks have already been performed on the block
	fn set_block_is_in_use(&mut self, block_addr: u64, in_use: bool) {
		let (byte, bit) = self.bitmap_entry_for_block_mut(block_addr - self.start_address());
		let mask = 1u8 << bit;
		*byte = ((*byte) & !mask) | (if in_use { mask } else { 0 });
	}

	fn find_buddy(&self, block_addr: u64, order: usize) -> Option<u64> {
		let block_size = byte_count_of_order(order);
		let block_offset = block_addr - self.start_address();

		// the buddy can be found by XORing the block size with its offset.
		// this works because the sizes are powers of two, so XORing simply toggles the bit
		// for that size. also, blocks must be aligned to their sizes (so any bits lower than the
		// bit for their size must be 0).
		let buddy_offset = block_offset ^ block_size;
		let maybe_buddy = buddy_offset + self.start_address();

		// make sure this is actually a valid buddy; we may have blocks near the end of the region that don't have buddies
		if maybe_buddy + block_size > self.start_address() + (self.page_count() * PAGE_SIZE) {
			None
		} else {
			Some(maybe_buddy)
		}
	}

	#[cfg(memory_ensure_block_state)]
	fn ensure_block_state(&self, block_addr: u64, in_use: bool) {
		// TODO: check if it's part of another block that *is* in-use
		if self.block_is_in_use(block_addr) != in_use {
			if in_use {
				panic!("Block was not in-use but should have been");
			} else {
				panic!("Block was in-use but should not have been");
			}
		}
	}

	#[cfg(not(pmm_ensure_block_state))]
	fn ensure_block_state(&self, _block_addr: u64, _in_use: bool) {
		// no-op
	}

	/// SAFETY: the block referred to by `block_addr` must be truly free: it must not be part of another block, neither one that's in-use nor one that's free.
	unsafe fn insert_free_block(&mut self, block_addr: u64, order: usize) {
		self.perform_basic_checks(block_addr, order);

		let block = Self::addr_to_insert_ptr(block_addr);

		#[cfg(debug_assertions)]
		{
			// TODO: check that the block is indeed free and not part of another block
		}

		let block_ref = &mut *block;
		block_ref.write(FreeBlock {
			link: Default::default(),
			addr: block_addr,
		});
		self.buckets_mut()[order].push_back(block_ref.assume_init_mut());
		self.set_block_is_in_use(block_addr, false);

		self.after_insert(block_addr, order);
	}

	/// SAFETY: `block` must be a valid free block within the bucket of the given order
	unsafe fn remove_free_block(&mut self, block: *mut FreeBlock, order: usize) {
		let block_addr = (&*block).addr;

		self.perform_basic_checks(block_addr, order);

		#[cfg(debug_assertions)]
		{
			// TODO: check that the block is indeed free and not part of another block
		}

		self.buckets_mut()[order]
			.cursor_mut_from_ptr(block)
			.remove()
			.expect("Expected block cursor to point to valid element");

		self.set_block_is_in_use(block_addr, true);

		self.after_remove(block, order);
	}

	fn remove_first_free_block(&mut self, order: usize) -> *mut FreeBlock {
		let block = self.buckets()[order].front().get().unwrap();

		// SAFETY: we have exclusive access to this region, so we also have exclusive access to its free blocks
		let ptr = (block as *const FreeBlock) as *mut FreeBlock;

		// SAFETY: we know this block came from the bucket of the given order, so this is perfectly safe
		unsafe { self.remove_free_block(ptr, order) };

		// SAFETY: no one else can possibly have this block now; we've removed it from the list
		ptr
	}

	fn find_candidate_block(
		&mut self,
		min_order: usize,
		alignment_power: u8,
		previous_candidate_order: &mut Option<usize>,
	) -> Option<CandidateBlockResult> {
		let alignment_mask = (1u64 << alignment_power) - 1;
		let orig_candidate_order: Option<usize> = *previous_candidate_order;
		let mut new_candidate_order: Option<usize> = None;

		let mut aligned_candidate_block = None;
		let mut aligned_candidate_order = None;

		for (index, bucket) in self.buckets_mut()[min_order..].iter_mut().enumerate() {
			let order = min_order + index;

			if order
				>= new_candidate_order
					.or(orig_candidate_order)
					.unwrap_or(usize::MAX)
			{
				break;
			}

			let block_cursor = bucket.front();
			let block = match block_cursor.get() {
				Some(x) => x,
				None => continue,
			};

			// SAFETY: this is a valid block because it came from the free block buckets of the region (which must only contain valid free blocks).
			let block_addr = (unsafe { &*(block as *const FreeBlock) }).addr;

			// if the address isn't aligned how we want it, let's see if maybe there's a sub-block that *is*
			if (block_addr & alignment_mask) != 0 {
				if order > min_order {
					let next_aligned_address =
						(block_addr & !alignment_mask) + (alignment_mask + 1);
					let mut subblock_end = block_addr + byte_count_of_order(order);

					if next_aligned_address > block_addr && next_aligned_address < subblock_end {
						// okay, great; the next aligned address falls within this block.
						// however, let's see if the sub-block is big enough for us.
						let mut subblock = block_addr;
						let mut suborder = order - 1;
						let mut found = false;

						while suborder >= min_order && subblock < subblock_end {
							if (subblock & alignment_mask) == 0 {
								// awesome, this sub-block is big enough and it's aligned properly
								found = true;
								// SAFETY: this one is twofold:
								//   * this is not aliased because we have exclusive access to the region list and free blocks must be unaliased
								//   * this is a valid pointer because it's a sub-block and we just made sure it's aligned and sized properly.
								aligned_candidate_block = Some(subblock);
								aligned_candidate_order = Some(suborder);
								break;
							} else {
								if next_aligned_address > subblock
									&& next_aligned_address
										< subblock + byte_count_of_order(suborder)
								{
									// okay, so this sub-block contains the address; let's search its sub-leaves
									if suborder == min_order {
										// can't split up a min order block to get an aligned block big enough
										break;
									} else {
										subblock_end = subblock + byte_count_of_order(suborder);
										suborder -= 1;
									}
								} else {
									// nope, this sub-block doesn't contain the address; let's skip it
									subblock += byte_count_of_order(suborder);
								}
							}
						}

						if !found {
							// none of this block's sub-blocks were big enough and aligned properly
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

			new_candidate_order = Some(order);

			if order == min_order {
				// we're not going to be able to find a smaller candidate block
				break;
			}
		}

		if new_candidate_order.is_some() {
			*previous_candidate_order = new_candidate_order;

			Some(CandidateBlockResult {
				order: new_candidate_order.unwrap(),
				aligned_address: aligned_candidate_block,
				aligned_order: aligned_candidate_order,
			})
		} else {
			None
		}
	}

	fn allocate_candidate(
		&mut self,
		min_order: usize,
		candidate: CandidateBlockResult,
		alignment_power: u8,
	) -> Option<u64> {
		let alignment_mask = (1u64 << alignment_power) - 1;

		let CandidateBlockResult {
			order: mut candidate_order,
			aligned_address: aligned_candidate_block,
			aligned_order: aligned_candidate_order,
		} = candidate;

		let candidate_block_ptr =
			self.buckets_mut()[candidate_order].front_mut().get()? as *const FreeBlock;

		// SAFETY: this is safe because this is a valid free block that we obtained from the candidate region's free block buckets
		let mut candidate_block_addr = (unsafe { &*candidate_block_ptr }).addr;

		// FIXME: we shouldn't implicitly trust the information in `candidate` and we should verify it ourselves

		self.ensure_block_state(candidate_block_addr, false);

		// okay, we've chosen our candidate region. un-free it
		assert_eq!(
			candidate_block_ptr,
			self.remove_first_free_block(candidate_order) as *const FreeBlock
		);

		Self::after_allocate_remove(candidate_order);

		if (candidate_block_addr & alignment_mask) != 0 {
			// alright, if we have an unaligned candidate block, we've already determined that
			// it does have an aligned sub-block big enough for us, so let's split up the block to get it.

			let aligned_candidate_block = aligned_candidate_block.unwrap();
			let aligned_candidate_order = aligned_candidate_order.unwrap();

			let mut block_end = candidate_block_addr + byte_count_of_order(candidate_order);
			let mut subblock = candidate_block_addr;
			let mut suborder = candidate_order - 1;

			let aligned_candidate_block_addr = aligned_candidate_block;

			while suborder >= aligned_candidate_order {
				let mut next_subblock = 0;

				for split_block in
					(subblock..block_end).step_by(byte_count_of_order(suborder) as usize)
				{
					if aligned_candidate_block_addr >= subblock
						&& aligned_candidate_block_addr < subblock + byte_count_of_order(suborder)
					{
						// this block either is the aligned candidate block or contains the aligned candidate block
						next_subblock = split_block;
					} else {
						// this is just a block we don't care about; add it back to the region
						//
						// SAFETY: we know this block is not aliased because it was part of a free block (and free blocks must be unaliased).
						//         additionally, we know this is a valid pointer because we derived it from a free block and made sure it was aligned
						//         to the correct order and sized properly.
						unsafe { self.insert_free_block(split_block, suborder) };
					}
				}

				if suborder == aligned_candidate_order {
					// this is the order of the aligned candidate block, so this next subblock MUST be the aligned candidate block
					assert_eq!(next_subblock, aligned_candidate_block_addr);

					candidate_block_addr = aligned_candidate_block_addr;
					candidate_order = aligned_candidate_order;
					break;
				} else {
					// this is NOT the order of the aligned candidate block, so this MUST NOT be the aligned candidate block
					assert_ne!(next_subblock, aligned_candidate_block_addr);

					// now let's iterate through this block's sub-blocks
					subblock = next_subblock;
					block_end = subblock + byte_count_of_order(suborder);
					suborder -= 1;
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
		let mut start_split = candidate_block_addr + byte_count_of_order(min_order);
		for order in min_order..candidate_order {
			// SAFETY: we know this block is not aliased because it was part of a free block (and free blocks must be unaliased).
			//         additionally, we know this is a valid pointer because we derived it from a free block and made sure it was aligned
			//         to the correct order and sized properly.
			unsafe { self.insert_free_block(start_split, order) };
			start_split += byte_count_of_order(order);
		}

		Some(candidate_block_addr)
	}

	/// Allocates the next available contiguous aligned block of the given size and alignment.
	///
	/// # Arguments
	///
	/// * `order` - The order of the block to allocate.
	/// * `alignment_power` - A power of two for the alignment that the allocated region should have.
	///                       For example, for 8-byte alignment, this should be 3 because 2^3 = 8.
	///                       A value of 0 is 2^0 = 1, which is normal, unaligned memory.
	/// * `regions` - A linked list of BuddyRegionHeaderWrappers that wrap BuddyRegionHeaders of this type.
	fn allocate_aligned<A: Adapter>(
		min_order: usize,
		alignment_power: u8,
		regions: &mut LinkedList<A>,
	) -> Result<u64, ()>
	where
		A::LinkOps: LinkedListOps,
		<<A as Adapter>::PointerOps as PointerOps>::Value: BuddyRegionHeaderWrapper<Wrapped = Self>,
	{
		if min_order >= MAX_ORDER {
			return Err(());
		}

		let mut candidate_order = None;

		// first, look for the smallest usable block from any region
		let result = regions
			.iter()
			.filter_map(|phys_region| {
				// SAFETY: we have exclusive access to the linked list, so this can't possibly be aliased.
				let inner = unsafe { phys_region.inner() };

				inner
					.find_candidate_block(min_order, alignment_power, &mut candidate_order)
					.map(|result| (inner, result))
			})
			.min_by_key(|item| item.1.order);

		// uh-oh, we don't have any free blocks big enough in any region
		if result.is_none() {
			return Err(());
		}

		// this is guaranteed to be the same block we found above since we have exclusive access to the list (so no one else can be modifying or even reading from any of the regions);
		let candidate_block_addr = {
			let unwrapped = result.unwrap();
			unwrapped
				.0
				.allocate_candidate(min_order, unwrapped.1, alignment_power)
				.unwrap()
		};

		// alright, we now have the right-size block.

		#[cfg(memory_log_alloc)]
		kprintln!(
			"Allocating block {} (order = {})",
			candidate_block_addr,
			min_order
		);

		// finally, we can return the new block
		Ok(candidate_block_addr)
	}

	/// Finds and returns a (mutable) reference to the given block's parent region.
	fn find_parent_region<'a, A: Adapter>(
		_order: usize,
		block_addr: u64,
		regions: &'a mut LinkedList<A>,
	) -> Option<&'a mut Self>
	where
		A::LinkOps: LinkedListOps,
		<<A as Adapter>::PointerOps as PointerOps>::Value: BuddyRegionHeaderWrapper<Wrapped = Self>,
	{
		regions.iter().find_map(|region| {
			// SAFETY: because we have exclusive access to the linked list, we are guaranteed to have exclusive access to its regions.
			let inner = unsafe { region.inner() };

			if block_addr >= inner.start_address()
				&& block_addr < inner.start_address() + (inner.page_count() * PAGE_SIZE)
			{
				Some(inner)
			} else {
				None
			}
		})
	}

	/// Frees/deallocates a block of memory previously allocated from the same region list.
	///
	/// # Safety
	///
	/// This function is unsafe because the block address given must be of the given order and must be a valid block that was previously allocated
	/// from the same list of regions.
	unsafe fn free<A: Adapter>(mut order: usize, mut block_addr: u64, regions: &mut LinkedList<A>)
	where
		A::LinkOps: LinkedListOps,
		<<A as Adapter>::PointerOps as PointerOps>::Value: BuddyRegionHeaderWrapper<Wrapped = Self>,
	{
		#[cfg(memory_log_alloc)]
		kprintln!("Freeing block {} (order = {})", block_addr, order);

		let parent_region = Self::find_parent_region(order, block_addr, regions)
			.expect("The block should belong to one of the regions in the given region list");

		if !parent_region.block_is_in_use(block_addr) {
			panic!("Attempt to free frame that wasn't allocated");
		}

		// mark this block as free since we might not be able to do so later
		// (if we merge with a buddy)
		parent_region.set_block_is_in_use(block_addr, false);

		// find buddies to merge with
		while order < MAX_ORDER {
			let buddy = match parent_region.find_buddy(block_addr, order) {
				Some(x) => x,

				// oh, no buddy? how sad :(
				None => break,
			};

			if parent_region.block_is_in_use(buddy) {
				// whelp, our buddy is in use. we can't do any more merging
				break;
			}

			// find our buddy and make sure they're of the order we're expecting
			let buddy_block = match parent_region.buckets()[order]
				.iter()
				.find(|&maybe_buddy| maybe_buddy.addr == buddy)
			{
				Some(x) => x,

				// oh, looks like our buddy isn't the right size so we can't merge with them.
				None => break,
			};

			// SAFETY: we have exclusive access to the region list, so we have exclusive access to its regions and, by extension, to their free blocks
			let buddy_block = (buddy_block as *const FreeBlock) as *mut FreeBlock;

			// yay, our buddy is free! let's get together.

			// take them out of their current bucket
			//
			// SAFETY: we've made sure that the buddy is actually free and part of the bucket of the given order.
			unsafe { parent_region.remove_free_block(buddy_block, order) };

			Self::after_free_buddy_remove(order);

			// whoever's got the lower address is the start of the bigger block
			if buddy < block_addr {
				block_addr = buddy;
			}

			// now *don't* insert the new block into the free list.
			// that would be pointless since we might still have a buddy to merge with the bigger block
			// and we insert it later, after the loop.

			order += 1;
		}

		// finally, insert the new (possibly merged) block into the appropriate bucket
		//
		// SAFETY: we've ensured that the block is truly free (and, thus, is unaliased) and we've only (potentially) merged it with other free blocks.
		unsafe { parent_region.insert_free_block(block_addr, order) };
	}
}
