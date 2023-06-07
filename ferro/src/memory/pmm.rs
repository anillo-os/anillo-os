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

use core::{
	cell::UnsafeCell,
	mem::{size_of, MaybeUninit},
	sync::atomic::{AtomicU64, Ordering},
};

use intrusive_collections::{intrusive_adapter, LinkedList, LinkedListAtomicLink};

use super::{
	order::{order_of_page_count_ceil, order_of_page_count_floor, page_count_of_order, MAX_ORDER},
	util::round_up_page_div,
	PhysicalAddress, PAGE_SIZE, PHYSICAL_MAPPED_BASE,
};
use crate::{
	memory::order::byte_count_of_order,
	sync::{Lock, SpinLock, SpinLockGuard},
	util::align_down_pow2,
	MemoryRegion, MemoryRegionType,
};

struct FreeBlock {
	link: LinkedListAtomicLink,
}

intrusive_adapter!(FreeBlockAdapter = &'static FreeBlock: FreeBlock { link: LinkedListAtomicLink });

struct InnerRegionHeader {
	page_count: u64,
	phys_start_addr: u64,
	buckets: [LinkedList<FreeBlockAdapter>; MAX_ORDER],
	bitmap: &'static mut [u8],
}

struct RegionHeader {
	link: LinkedListAtomicLink,
	inner: UnsafeCell<InnerRegionHeader>,
}

impl RegionHeader {
	const BITMAP_SPACE: u64 = PAGE_SIZE - (size_of::<RegionHeader>() as u64);
}

// SAFETY: this is not safe to copy between threads, but it *is* safe to access between threads
//         when protected by a lock. to ensure we use RegionHeader in a thread-safe way, we must
//         ONLY access it while holding the REGIONS lock and always access it by reference
//         (we must NEVER move it).
unsafe impl Send for RegionHeader {}
unsafe impl Sync for RegionHeader {}

impl InnerRegionHeader {
	fn bitmap_entry_for_block(&self, block_offset: u64) -> (&u8, u8) {
		let bitmap_index = block_offset / PAGE_SIZE;
		let byte_index = bitmap_index / 8;
		let bit_index = bitmap_index % 8;

		(&self.bitmap[byte_index as usize], bit_index as u8)
	}

	fn bitmap_entry_for_block_mut(&mut self, block_offset: u64) -> (&mut u8, u8) {
		let bitmap_index = block_offset / PAGE_SIZE;
		let byte_index = bitmap_index / 8;
		let bit_index = bitmap_index % 8;

		(&mut self.bitmap[byte_index as usize], bit_index as u8)
	}

	fn perform_basic_checks(&self, block: *const MaybeUninit<FreeBlock>, order: usize) {
		// as basic (and cheap) sanity checks, ensure:
		//   * the block pointer lies within our usable region
		//   * the block pointer is aligned to the block size for the given order
		assert!(
			(block as u64) >= self.phys_start_addr
				&& (block as u64) < self.phys_start_addr + self.page_count * PAGE_SIZE
		);
		let offset = (block as u64) - self.phys_start_addr;
		assert_eq!(
			offset - align_down_pow2(offset, byte_count_of_order(order)),
			0
		);
	}

	pub fn block_is_in_use(&self, block: *const MaybeUninit<FreeBlock>) -> bool {
		// we don't care about the order here, just make sure it's at least a valid order-0 block.
		self.perform_basic_checks(block, 0);
		let (byte, bit) = self.bitmap_entry_for_block((block as u64) - self.phys_start_addr);
		((*byte) & (1u8 << bit)) != 0
	}

	// NOTE: this private fn assumes that basic sanity checks have already been performed on the block
	fn set_block_is_in_use(&mut self, block: *const MaybeUninit<FreeBlock>, in_use: bool) {
		let (byte, bit) = self.bitmap_entry_for_block_mut((block as u64) - self.phys_start_addr);
		let mask = 1u8 << bit;
		*byte = ((*byte) & !mask) | (if in_use { mask } else { 0 });
	}

	/// SAFETY: `block` must be truly free: it must not be part of another block, neither one that's in-use nor one that's free.
	pub unsafe fn insert_free_block(&mut self, block: *mut MaybeUninit<FreeBlock>, order: usize) {
		self.perform_basic_checks(block, order);

		#[cfg(debug_assertions)]
		{
			// TODO: check that the block is indeed free and not part of another block
		}

		let block_ref = &mut *block;
		block_ref.write(FreeBlock {
			link: Default::default(),
		});
		self.buckets[order].push_back(block_ref.assume_init_mut());
		self.set_block_is_in_use(block, false);

		FRAMES_IN_USE.fetch_sub(page_count_of_order(order), Ordering::Relaxed);
	}

	/// SAFETY: `block` must be a valid free block within the bucket of the given order
	pub unsafe fn remove_free_block(&mut self, block: *mut FreeBlock, order: usize) {
		self.perform_basic_checks(block as *mut MaybeUninit<FreeBlock>, order);

		#[cfg(debug_assertions)]
		{
			// TODO: check that the block is indeed free and not part of another block
		}

		self.buckets[order]
			.cursor_mut_from_ptr(block)
			.remove()
			.expect("Expected block cursor to point to valid element");

		self.set_block_is_in_use(block as *const MaybeUninit<FreeBlock>, true);
	}

	pub fn remove_first_free_block(&mut self, order: usize) -> *mut FreeBlock {
		let ptr =
			(self.buckets[order].front().get().unwrap() as *const FreeBlock) as *mut FreeBlock;

		// SAFETY: we know this block came from the bucket of the given order, so this is perfectly safe
		unsafe { self.remove_free_block(ptr, order) };

		// SAFETY: no one else can possibly have this block now; we've removed it from the list
		ptr
	}

	#[cfg(pmm_ensure_block_state)]
	pub fn ensure_block_state(&self, block: *const MaybeUninit<FreeBlock>, in_use: bool) {
		if self.block_is_in_use(block) != in_use {
			if in_use {
				panic!("Block was not in-use but should have been");
			} else {
				panic!("Block was in-use but should not have been");
			}
		}
	}

	#[cfg(not(pmm_ensure_block_state))]
	pub fn ensure_block_state(&self, _block: *const MaybeUninit<FreeBlock>, _in_use: bool) {
		// no-op
	}

	fn find_buddy(
		&self,
		block: *const MaybeUninit<FreeBlock>,
		order: usize,
	) -> Option<*const MaybeUninit<FreeBlock>> {
		let block_addr = block as u64;
		let block_size = byte_count_of_order(order);
		let block_offset = block_addr - self.phys_start_addr;

		// the buddy can be found by XORing the block size with its offset.
		// this works because the sizes are powers of two, so XORing simply toggles the bit
		// for that size. also, blocks must be aligned to their sizes (so any bits lower than the
		// bit for their size must be 0).
		let buddy_offset = block_offset ^ block_size;
		let maybe_buddy = buddy_offset + self.phys_start_addr;

		// make sure this is actually a valid buddy; we may have blocks near the end of the region that don't have buddies
		if maybe_buddy + block_size > self.phys_start_addr + (self.page_count * PAGE_SIZE) {
			None
		} else {
			Some(maybe_buddy as *const MaybeUninit<FreeBlock>)
		}
	}
}

intrusive_adapter!(RegionHeaderAdapter = &'static RegionHeader: RegionHeader { link: LinkedListAtomicLink });

static REGIONS: SpinLock<LinkedList<RegionHeaderAdapter>> =
	SpinLock::new(LinkedList::new(RegionHeaderAdapter::NEW));

static mut TOTAL_FRAMES: u64 = 0;
static FRAMES_IN_USE: AtomicU64 = AtomicU64::new(0);

pub(super) fn initialize(memory_regions: &[MemoryRegion]) -> Result<(), ()> {
	let mut regions = REGIONS.lock();

	for region in memory_regions {
		// skip non-general memory
		if region.ty != MemoryRegionType::General {
			continue;
		}

		if region.page_count < 2 {
			// we need at least 2 pages for a valid region, since we require 1 page for the header
			continue;
		}

		let phys_start = region.physical_start as u64
			+ if region.physical_start == 0 {
				// skip the null address
				PAGE_SIZE
			} else {
				0
			} + PHYSICAL_MAPPED_BASE;
		let mut page_count =
			(region.page_count as u64 - 1) - if region.physical_start == 0 { 1 } else { 0 };

		let bitmap_byte_count = (page_count + 7) / 8;

		let mut extra_bitmap_page_count = 0;
		if bitmap_byte_count >= RegionHeader::BITMAP_SPACE {
			// extra pages are required for the bitmap
			extra_bitmap_page_count =
				round_up_page_div(bitmap_byte_count - RegionHeader::BITMAP_SPACE);
			if extra_bitmap_page_count > page_count {
				// weird; not enough space for bitmap
				continue;
			}
			page_count -= extra_bitmap_page_count;
		}

		// if we got here, we're definitely going to use this region

		let header_val = RegionHeader {
			link: Default::default(),
			inner: UnsafeCell::new(InnerRegionHeader {
				page_count,
				phys_start_addr: phys_start + (1 + extra_bitmap_page_count) * PAGE_SIZE,
				buckets: Default::default(),
				// SAFETY: this bitmap starts right after the end of the region header and extends to all the pages we reserved for it.
				//         additionally, alignment is not an issue because the `u8`s have single byte alignment.
				bitmap: unsafe {
					core::slice::from_raw_parts_mut(
						(phys_start + size_of::<RegionHeader>() as u64) as *mut u8,
						(RegionHeader::BITMAP_SPACE + extra_bitmap_page_count * PAGE_SIZE) as usize,
					)
				},
			}),
		};

		// SAFETY: this is safe because no one else could possibly be accessing this region right now (we're the only code running at the moment)
		//         and we don't have any other pointers to this region anywhere
		let header = unsafe { &mut *(phys_start as *mut MaybeUninit<RegionHeader>) };
		header.write(header_val);
		// SAFETY: we just initialized it above
		let header = unsafe { header.assume_init_mut() };
		let inner_header = header.inner.get_mut();

		// clear out the bitmap
		inner_header.bitmap.fill(0);

		let mut block_addr = inner_header.phys_start_addr;
		while page_count > 0 {
			let order = order_of_page_count_floor(page_count);
			let pages = page_count_of_order(order);

			// SAFETY: we are inserting non-overlapping free blocks from within the region
			unsafe { inner_header.insert_free_block(block_addr as *mut _, order) };

			block_addr += pages * PAGE_SIZE;
			page_count -= pages;
		}

		// SAFETY: we're currently initializing the PMM, so no one else is writing to this variable.
		unsafe { TOTAL_FRAMES += inner_header.page_count };
		FRAMES_IN_USE.fetch_add(inner_header.page_count, Ordering::Relaxed);

		regions.push_back(header);
	}

	#[cfg(pmm_debug)]
	{
		for region in regions.iter() {
			// SAFETY: no one else can possibly be accessing this right now (we're the only code running at the moment)
			let inner = unsafe { &*region.inner.get() };
			for (order, bucket) in (&inner.buckets).iter().enumerate() {
				for block in bucket.iter() {
					kprint!(
						"free block @ {:#x}, {} page(s); ",
						(block as *const FreeBlock) as u64,
						page_count_of_order(order)
					);
				}
			}
		}
		kprintln!();
	}

	Ok(())
}

pub fn total_frames() -> u64 {
	// SAFETY: we only write to this once, during initialization
	unsafe { TOTAL_FRAMES }
}

pub fn frames_in_use() -> u64 {
	FRAMES_IN_USE.load(Ordering::Relaxed)
}

pub struct PhysicalFrame {
	addr: PhysicalAddress,
	page_count: u64,
	allocated: bool,
}

impl PhysicalFrame {
	const MIN_ALIGNMENT: u8 = PAGE_SIZE.ilog2() as u8;

	/// Allocates the next available contiguous physical region of the given size.
	///
	/// # Arguments
	///
	/// * `page_count` - Number of pages to allocate for the region.
	pub fn allocate(page_count: u64) -> Result<Self, ()> {
		Self::allocate_aligned(page_count, 0)
	}

	/// Allocates the next available contiguous aligned physical region of the given size and alignment.
	///
	/// # Arguments
	///
	/// * `page_count` - Number of pages to allocate for the region.
	/// * `alignment_power` - A power of two for the alignment that the allocated region should have.
	///                       For example, for 8-byte alignment, this should be 3 because 2^3 = 8.
	///                       A value of 0 is 2^0 = 1, which is normal, unaligned memory.
	///                       Note that only values in the range `0..=39` are allowed, meaning the maximum possible alignment is 512GiB.
	pub fn allocate_aligned(page_count: u64, mut alignment_power: u8) -> Result<Self, ()> {
		if alignment_power < Self::MIN_ALIGNMENT {
			alignment_power = Self::MIN_ALIGNMENT;
		}

		if alignment_power > 39 {
			return Err(());
		}

		let alignment_mask = (1u64 << alignment_power) - 1;
		let min_order = order_of_page_count_ceil(page_count);

		let regions = REGIONS.lock();

		let mut candidate_order = None;

		// first, look for the smallest usable block from any region
		let result = regions
			.iter()
			.filter_map(|phys_region| {
				// SAFETY: we're holding the REGIONS lock, so this can't possibly be aliased.
				let inner = unsafe { &mut *phys_region.inner.get() };

				let mut aligned_candidate_block = None;
				let mut aligned_candidate_order = None;

				for (index, bucket) in inner.buckets[min_order..].iter_mut().enumerate() {
					let order = min_order + index;

					if order >= candidate_order.unwrap_or(usize::MAX) {
						break;
					}

					let phys_block_cursor = bucket.front();
					let phys_block = match phys_block_cursor.get() {
						Some(x) => x,
						None => continue,
					};

					let phys_block_addr = (phys_block as *const FreeBlock) as u64;

					// if the address isn't aligned how we want it, let's see if maybe there's a sub-block that *is*
					if (phys_block_addr & alignment_mask) != 0 {
						if order > min_order {
							let next_aligned_address =
								(phys_block_addr & !alignment_mask) + (alignment_mask + 1);
							let mut subblock_end = phys_block_addr + byte_count_of_order(order);

							if next_aligned_address > phys_block_addr
								&& next_aligned_address < subblock_end
							{
								// okay, great; the next aligned address falls within this block.
								// however, let's see if the sub-block is big enough for us.
								let mut subblock = phys_block_addr;
								let mut suborder = order - 1;
								let mut found = false;

								while suborder >= min_order && subblock < subblock_end {
									if (subblock & alignment_mask) == 0 {
										// awesome, this sub-block is big enough and it's aligned properly
										found = true;
										// SAFETY: this one is twofold:
										//   * this is not aliased because we're holding the REGIONS lock and free blocks must be unaliased
										//   * this is a valid pointer because it's a sub-block and we just made sure it's aligned and sized properly.
										aligned_candidate_block = Some(unsafe {
											&mut *(subblock as *mut MaybeUninit<FreeBlock>)
										});
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
												subblock_end =
													subblock + byte_count_of_order(suborder);
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

					candidate_order = Some(order);
					break;
				}

				if candidate_order.is_some() {
					Some((
						candidate_order.unwrap(),
						inner,
						aligned_candidate_block,
						aligned_candidate_order,
					))
				} else {
					None
				}
			})
			.min_by_key(|item| item.0);

		// uh-oh, we don't have any free blocks big enough in any region
		if result.is_none() {
			return Err(());
		}

		let (
			mut candidate_order,
			candidate_parent_region,
			aligned_candidate_block,
			aligned_candidate_order,
		) = result.unwrap();

		// this is guaranteed to be the same block we found above since we're holding the REGIONS lock (so no one else can be modifying or even reading from any of the regions)
		let mut candidate_block_ptr = (candidate_parent_region.buckets[candidate_order]
			.front_mut()
			.get()
			.unwrap() as *const FreeBlock)
			as *const MaybeUninit<FreeBlock>;

		candidate_parent_region.ensure_block_state(candidate_block_ptr, false);

		// okay, we've chosen our candidate region. un-free it
		let old_block_ptr = candidate_block_ptr;
		candidate_block_ptr = candidate_parent_region.remove_first_free_block(candidate_order)
			as *const MaybeUninit<FreeBlock>;
		assert_eq!(old_block_ptr, candidate_block_ptr);

		FRAMES_IN_USE.fetch_add(page_count_of_order(candidate_order), Ordering::Relaxed);

		let mut candidate_block_addr = candidate_block_ptr as u64;

		if (candidate_block_addr & alignment_mask) != 0 {
			// alright, if we have an unaligned candidate block, we've already determined that
			// it does have an aligned sub-block big enough for us, so let's split up the block to get it.

			let aligned_candidate_block = aligned_candidate_block.unwrap();
			let aligned_candidate_order = aligned_candidate_order.unwrap();

			let mut block_end = candidate_block_addr + byte_count_of_order(candidate_order);
			let mut subblock = candidate_block_addr;
			let mut suborder = candidate_order - 1;

			let aligned_candidate_block_addr =
				(aligned_candidate_block as *const MaybeUninit<FreeBlock>) as u64;

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
						unsafe {
							candidate_parent_region.insert_free_block(
								split_block as *mut MaybeUninit<FreeBlock>,
								suborder,
							)
						};
					}
				}

				if suborder == aligned_candidate_order {
					// this is the order of the aligned candidate block, so this next subblock MUST be the aligned candidate block
					assert_eq!(next_subblock, aligned_candidate_block_addr);

					// initialize it before assigning it
					candidate_block_ptr = (aligned_candidate_block.write(FreeBlock {
						link: Default::default(),
					}) as *mut FreeBlock) as *const MaybeUninit<FreeBlock>;
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

		// update the candidate block address (since it may have changed above)
		candidate_block_addr = candidate_block_ptr as u64;

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
			unsafe {
				candidate_parent_region
					.insert_free_block(start_split as *mut MaybeUninit<FreeBlock>, order)
			};
			start_split += byte_count_of_order(order);
		}

		// alright, we now have the right-size block.

		#[cfg(pmm_log_frames)]
		kprintln!(
			"Allocating frame {} (order = {})",
			candidate_block_addr - PHYSICAL_MAPPED_BASE,
			min_order
		);

		// finally, we can return the new block
		//
		// the stored physical address is offset by the physical map base address;
		// subtract that to get the actual physical address
		Ok(Self {
			addr: PhysicalAddress::new(candidate_block_addr - PHYSICAL_MAPPED_BASE),
			page_count,
			allocated: true,
		})
	}

	/// Creates a PhysicalFrame from the given address and page count, assuming it is memory that has been previously allocated by a PhysicalFrame
	/// and returned by [`Self::detach`].
	///
	/// # Safety
	///
	/// This method and its sibling, [`Self::from_unallocated()`], are unsafe for two reasons:
	///   * The given address and page count must refer to a valid memory region.
	///   * The memory region must be completely unowned by anyone else. In the current PMM implementation, this means that it must not be owned by
	///     another PhysicalFrame (since no other code allocates and deallocates physical frames).
	pub unsafe fn from_allocated(addr: PhysicalAddress, page_count: u64) -> Self {
		Self {
			addr,
			page_count,
			allocated: true,
		}
	}

	/// Creates a PhysicalFrame from the given address and page count, assuming it is *not* memory that has been previously allocated.
	///
	/// The given memory region **must** remain valid for as long as the PhysicalFrame does.
	///
	/// See [`Self::from_allocated`] for more information.
	pub unsafe fn from_unallocated(addr: PhysicalAddress, page_count: u64) -> Self {
		Self {
			addr,
			page_count,
			allocated: false,
		}
	}

	/// Consumes this PhysicalFrame and the associated physical address and page count *without* deallocating the frame.
	///
	/// Ordinarily, PhysicalFrames created via allocation methods ([`Self::allocate()`] and [`Self::allocate_aligned()`]) are deallocated when
	/// dropped. This method can be used to consume/drop an allocated PhysicalFrame while holding on to the frame itself.
	///
	/// To re-attach the memory to a PhysicalFrame which will deallocate it when dropped, use the [`Self::from_allocated()`] method.
	pub fn detach(self) -> (PhysicalAddress, u64) {
		let result = (self.addr, self.page_count);
		core::mem::forget(self);
		result
	}

	pub fn address(&self) -> PhysicalAddress {
		self.addr
	}

	pub fn page_count(&self) -> u64 {
		self.page_count
	}

	/// Finds and returns a (mutable) reference to this frame's parent region. Panics if none was found (since all frames must have a valid parent region).
	///
	/// Note that you must pass a spin-lock guard in to ensure that we have exclusive access to the region list.
	/// The returned reference is guaranteed to live for as long as the guard does. The returned reference can safely be used to mutate the region
	/// since you are holding the spin-lock guard (and thus have exclusive access to the list and all the regions within it).
	fn find_parent_region<'a>(
		&self,
		regions: &'a SpinLockGuard<LinkedList<RegionHeaderAdapter>>,
	) -> &'a mut InnerRegionHeader {
		let mapped_addr = self.addr.0 + PHYSICAL_MAPPED_BASE;

		regions
			.iter()
			.find_map(|region| {
				// SAFETY: because we are holding the spin-lock, we are guaranteed to have exclusive access to the region list and its regions.
				let inner = unsafe { &mut *region.inner.get() };

				if mapped_addr >= inner.phys_start_addr
					&& mapped_addr < inner.phys_start_addr + (inner.page_count * PAGE_SIZE)
				{
					Some(inner)
				} else {
					None
				}
			})
			.expect("All physical frames must belong to a parent region")
	}
}

impl Drop for PhysicalFrame {
	fn drop(&mut self) {
		if !self.allocated {
			return;
		}

		let mut order = order_of_page_count_ceil(self.page_count);

		#[cfg(pmm_log_frames)]
		kprintln!("Freeing frame {} (order = {})", self.addr.0, order);

		let regions = REGIONS.lock();

		let parent_region = self.find_parent_region(&regions);
		let mut block = (self.addr.0 + PHYSICAL_MAPPED_BASE) as *mut MaybeUninit<FreeBlock>;

		if !parent_region.block_is_in_use(block) {
			panic!("Attempt to free frame that wasn't allocated");
		}

		// mark this block as free since we might not be able to do so later
		// (if we merge with a buddy)
		parent_region.set_block_is_in_use(block, false);

		// find buddies to merge with
		while order < MAX_ORDER {
			let buddy = match parent_region.find_buddy(block, order) {
				Some(x) => x,

				// oh, no buddy? how sad :(
				None => break,
			};

			if parent_region.block_is_in_use(buddy) {
				// whelp, our buddy is in use. we can't do any more merging
				break;
			}

			// make sure our buddy is of the order we're expecting
			if !parent_region.buckets[order].iter().any(|maybe_buddy| {
				(maybe_buddy as *const FreeBlock) as *const MaybeUninit<FreeBlock> == buddy
			}) {
				// oh, looks like our buddy isn't the right size so we can't merge with them.
				break;
			}

			// yay, our buddy is free! let's get together.

			// take them out of their current bucket
			//
			// SAFETY: we've made sure that the buddy is actually free and part of the bucket of the given order.
			unsafe { parent_region.remove_free_block(buddy as *mut FreeBlock, order) };

			FRAMES_IN_USE.fetch_add(page_count_of_order(order), Ordering::Relaxed);

			// whoever's got the lower address is the start of the bigger block
			if buddy < block {
				block = buddy as *mut MaybeUninit<FreeBlock>;
			}

			// now *don't* insert the new block into the free list.
			// that would be pointless since we might still have a buddy to merge with the bigger block
			// and we insert it later, after the loop.

			order += 1;
		}

		// finally, insert the new (possibly merged) block into the appropriate bucket
		//
		// SAFETY: we've ensured that the block is truly free (and, thus, is unaliased) and we've only (potentially) merged it with other free blocks.
		unsafe { parent_region.insert_free_block(block, order) };
	}
}
