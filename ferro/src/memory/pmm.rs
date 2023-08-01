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
	region::FreeBlockAdapter,
	region::{BuddyRegionHeader, BuddyRegionHeaderWrapper},
	util::round_up_page_div,
	PhysicalAddress, PAGE_SIZE, PHYSICAL_MAPPED_BASE,
};
use crate::{
	sync::{Lock, SpinLock},
	MemoryRegion, MemoryRegionType,
};

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

impl BuddyRegionHeader for InnerRegionHeader {
	fn bitmap(&self) -> &[u8] {
		&self.bitmap
	}

	fn bitmap_mut(&mut self) -> &mut [u8] {
		&mut self.bitmap
	}

	fn start_address(&self) -> u64 {
		self.phys_start_addr
	}

	fn page_count(&self) -> u64 {
		self.page_count
	}

	fn buckets(&self) -> &[LinkedList<FreeBlockAdapter>; MAX_ORDER] {
		&self.buckets
	}

	fn buckets_mut(&mut self) -> &mut [LinkedList<FreeBlockAdapter>; MAX_ORDER] {
		&mut self.buckets
	}

	fn after_insert(&mut self, _block_addr: u64, order: usize) {
		FRAMES_IN_USE.fetch_sub(page_count_of_order(order), Ordering::Relaxed);
	}

	fn after_allocate_remove(order: usize) {
		FRAMES_IN_USE.fetch_add(page_count_of_order(order), Ordering::Relaxed);
	}

	fn after_free_buddy_remove(order: usize) {
		FRAMES_IN_USE.fetch_add(page_count_of_order(order), Ordering::Relaxed);
	}
}

impl BuddyRegionHeaderWrapper for RegionHeader {
	type Wrapped = InnerRegionHeader;

	unsafe fn inner(&self) -> &mut Self::Wrapped {
		&mut *self.inner.get()
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
			unsafe { inner_header.insert_free_block(block_addr, order) };

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

		let min_order = order_of_page_count_ceil(page_count);
		let mut regions = REGIONS.lock();

		InnerRegionHeader::allocate_aligned(min_order, alignment_power, &mut regions).map(
			|result| Self {
				// the stored physical address is offset by the physical map base address;
				// subtract that to get the actual physical address
				addr: PhysicalAddress::new(result - PHYSICAL_MAPPED_BASE),
				page_count,
				allocated: true,
			},
		)
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

	/// Consumes this PhysicalFrame and returns the associated physical address and page count *without* deallocating the frame.
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

	pub fn owned(&self) -> bool {
		self.allocated
	}
}

impl Drop for PhysicalFrame {
	fn drop(&mut self) {
		if !self.allocated {
			return;
		}

		let order = order_of_page_count_ceil(self.page_count);
		let mut regions = REGIONS.lock();

		// SAFETY: we know this is a valid block within the region list because we either allocated it from the list ourselves (using `Self::allocate` or
		//         `Self::allocate_aligned`) or we were told that it was previously allocated from it (using `Self::from_allocated`).
		unsafe { BuddyRegionHeader::free(order, self.addr.0 + PHYSICAL_MAPPED_BASE, &mut regions) };
	}
}
