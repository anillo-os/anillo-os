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
};

use intrusive_collections::{intrusive_adapter, LinkedList, LinkedListAtomicLink};

use super::{
	order::{order_of_page_count, page_count_of_order, MAX_ORDER},
	util::round_up_page_div,
	PhysicalAddress, PAGE_SIZE, PHYSICAL_MAPPED_BASE,
};
use crate::{
	memory::order::byte_count_of_order,
	sync::{Lock, SpinLock},
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

	fn perform_basic_checks(&self, block: *mut MaybeUninit<FreeBlock>, order: usize) {
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

	pub fn block_is_in_use(&self, block: *mut MaybeUninit<FreeBlock>) -> bool {
		// we don't care about the order here, just make sure it's at least a valid order-0 block.
		self.perform_basic_checks(block, 0);
		let (byte, bit) = self.bitmap_entry_for_block((block as u64) - self.phys_start_addr);
		((*byte) & (1u8 << bit)) != 0
	}

	// NOTE: this private fn assumes that basic sanity checks have already been performed on the block
	fn set_block_is_in_use(&mut self, block: *mut MaybeUninit<FreeBlock>, in_use: bool) {
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
	}
}

intrusive_adapter!(RegionHeaderAdapter = &'static RegionHeader: RegionHeader { link: LinkedListAtomicLink });

static REGIONS: SpinLock<LinkedList<RegionHeaderAdapter>> =
	SpinLock::new(LinkedList::new(RegionHeaderAdapter::NEW));

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
			};
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
		let header = unsafe {
			&mut *((phys_start + PHYSICAL_MAPPED_BASE) as *mut MaybeUninit<RegionHeader>)
		};
		header.write(header_val);
		// SAFETY: we just initialized it above
		let header = unsafe { header.assume_init_mut() };
		let inner_header = header.inner.get_mut();

		// clear out the bitmap
		inner_header.bitmap.fill(0);

		let mut block_addr = inner_header.phys_start_addr;
		while page_count > 0 {
			let order = order_of_page_count(page_count);
			let pages = page_count_of_order(order);

			// SAFETY: we are inserting non-overlapping free blocks from within the region
			unsafe { inner_header.insert_free_block(block_addr as *mut _, order) };

			block_addr += pages * PAGE_SIZE;
			page_count -= pages;
		}

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

pub struct PhysicalFrame {
	addr: PhysicalAddress,
	page_count: usize,
}

impl PhysicalFrame {
	/// Allocates the next available contiguous physical region of the given size.
	///
	/// # Arguments
	///
	/// * `page_count` - Number of pages to allocate for the region.
	pub fn allocate(page_count: usize) -> Result<Self, ()> {
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
	pub fn allocate_aligned(page_count: usize, alignment_power: u8) -> Result<Self, ()> {
		unimplemented!()
	}
}

impl Drop for PhysicalFrame {
	fn drop(&mut self) {
		unimplemented!()
	}
}
