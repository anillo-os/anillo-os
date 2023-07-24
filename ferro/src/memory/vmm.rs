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
	mem::{align_of, size_of, MaybeUninit},
	sync::atomic::{AtomicUsize, Ordering},
};

use intrusive_collections::{intrusive_adapter, LinkedList, LinkedListAtomicLink};
use static_assertions::const_assert;

use super::{
	make_virtual_address,
	order::{order_of_page_count_ceil, order_of_page_count_floor, page_count_of_order, MAX_ORDER},
	pmm::PhysicalFrame,
	region::{
		BuddyRegionHeader, BuddyRegionHeaderWrapper, CandidateBlockResult, FreeBlock,
		FreeBlockAdapter,
	},
	PageTable, PhysicalAddress, KERNEL_L4_START, PAGE_OFFSET_MAX, PAGE_SIZE, PAGE_TABLE_INDEX_MAX,
	PHYSICAL_MEMORY_L4_INDEX,
};
use crate::{
	sync::SpinLock,
	util::{align_down_pow2, closest_pow2_floor},
};

// TODO: we don't need whole pages/frames to store free virtual blocks.
//       we can instead group them together on allocated frames.

/// The order of an L4 page table entry.
///
/// Each L4 entry spans 512 GiB. This order corresponds to 2^27 * 4096 bytes, which is 512 GiB.
const L4_ORDER: usize = 27;

struct InnerRegionHeader {
	page_count: u64,
	virt_start_addr: u64,
	buckets: [LinkedList<FreeBlockAdapter>; MAX_ORDER],
	bitmap: &'static mut [u8],
}

struct RegionHeader {
	link: LinkedListAtomicLink,
	inner: UnsafeCell<InnerRegionHeader>,
}

const_assert!(align_of::<RegionHeader>() as u64 <= PAGE_SIZE);

impl RegionHeader {
	const BITMAP_ENTRIES: u64 = Self::BITMAP_SPACE * 8;
	const BITMAP_SPACE: u64 = PAGE_SIZE - (size_of::<RegionHeader>() as u64);
}

impl BuddyRegionHeaderWrapper for RegionHeader {
	type Wrapped = InnerRegionHeader;

	unsafe fn inner(&self) -> &mut Self::Wrapped {
		&mut *self.inner.get()
	}
}

// SAFETY: like in the PMM, this is not safe to copy between threads, but it *is* safe to access between threads
//         when protected by a lock. to ensure we use RegionHeader in a thread-safe way, we must
//         ONLY access it while holding the lock for it and always access it by reference
//         (we must NEVER move it).
unsafe impl Send for RegionHeader {}
unsafe impl Sync for RegionHeader {}

impl BuddyRegionHeader for InnerRegionHeader {
	fn bitmap(&self) -> &[u8] {
		&self.bitmap
	}

	fn bitmap_mut(&mut self) -> &mut [u8] {
		self.bitmap
	}

	fn start_address(&self) -> u64 {
		self.virt_start_addr
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

	fn after_remove(&mut self, block: *mut FreeBlock, _order: usize) {
		// SAFETY: we know this was a valid free block, so it must have a valid frame allocation backing it.
		let frame = unsafe { PhysicalFrame::from_allocated(PhysicalAddress::new(block as u64), 1) };
		// explicitly drop it
		drop(frame);
	}

	unsafe fn addr_to_insert_ptr(_block_addr: u64) -> *mut MaybeUninit<FreeBlock> {
		const_assert!(size_of::<FreeBlock>() as u64 <= PAGE_SIZE);

		let frame = PhysicalFrame::allocate(1)
			.expect("Allocating a physical frame for a free virtual block should succeed");
		frame.detach().0.as_mut_ptr()
	}
}

intrusive_adapter!(RegionHeaderAdapter = &'static RegionHeader: RegionHeader { link: LinkedListAtomicLink });

static ADDRESS_SPACE_ID_COUNTER: AtomicUsize = AtomicUsize::new(0);

const RESERVED_L4_INDICES: [usize; 2] = [PHYSICAL_MEMORY_L4_INDEX, KERNEL_L4_START];

struct InnerAddressSpace {
	id: usize,
	table_frame: PhysicalFrame,
	higher_half: LinkedList<RegionHeaderAdapter>,
	/// This is the address one byte AFTER the last usable byte of the next free region with the highest address.
	next_higher_half_top: u64,
	lower_half: LinkedList<RegionHeaderAdapter>,
	/// Same as Self::next_higher_half_top, but for the lower-half.
	next_lower_half_top: u64,
}

impl InnerAddressSpace {
	const MINIMUM_REGION_PAGES: u64 = Self::MINIMUM_REGION_SIZE / PAGE_SIZE;
	/// The minimum size (in bytes) for new regions allocated in address spaces.
	///
	/// This value is derived from the leftover space in a frame after reserving the beginning for the region header.
	/// Each leftover byte is part of a bitmap that where a byte can represent 8 blocks and each block is the size of a page.
	/// This value is then truncated to be a power of 2.
	///
	/// On platforms where frames are 4 KiB wide, the initial value is 110.5 MiB, which is then truncated to 64 MiB.
	/// On platforms where frames are 16 KiB wide, the initial value is 1.9 GiB, which is then truncated to 1 GiB.
	/// On platforms where frames are 64 KiB wide, the initial value is 31.7 GiB, which is then truncated to 16 GiB.
	const MINIMUM_REGION_SIZE: u64 = closest_pow2_floor(RegionHeader::BITMAP_ENTRIES * PAGE_SIZE);

	fn table(&mut self) -> &mut PageTable {
		const_assert!(size_of::<PageTable>() as u64 <= PAGE_SIZE);
		assert_eq!(
			align_down_pow2(
				self.table_frame.address().as_value(),
				align_of::<PageTable>() as u64
			),
			self.table_frame.address().as_value()
		);
		// SAFETY: since we have `&mut self`, we have exclusive access to the page table.
		//         additionally, we just made sure above that this is a valid pointer to a page table.
		unsafe { &mut *self.table_frame.address().as_mut_ptr() }
	}

	fn new() -> Self {
		Self {
			id: ADDRESS_SPACE_ID_COUNTER.fetch_add(1, Ordering::Relaxed),
			table_frame: PhysicalFrame::allocate(1)
				.expect("Allocating a frame for the address space's root table should succeed"),
			higher_half: LinkedList::new(RegionHeaderAdapter::NEW),
			next_higher_half_top: make_virtual_address(PHYSICAL_MEMORY_L4_INDEX as u16, 0, 0, 0, 0),
			lower_half: LinkedList::new(RegionHeaderAdapter::NEW),
			// note that this does *NOT* produce the same result as `make_virtual_address(KERNEL_L4_START, 0, 0, 0, 0)`
			// because that will automatically set the high bits to make a canonical higher-half address.
			next_lower_half_top: make_virtual_address(
				(KERNEL_L4_START - 1) as u16,
				PAGE_TABLE_INDEX_MAX,
				PAGE_TABLE_INDEX_MAX,
				PAGE_TABLE_INDEX_MAX,
				PAGE_OFFSET_MAX,
			) + 1,
		}
	}

	/// Allocates a new region with the minimum given page count and alignment in either the lower or higher half (as specified).
	fn allocate_region(
		&mut self,
		mut min_page_count: u64,
		alignment_power: u8,
		in_higher_half: bool,
	) -> Option<(&RegionHeader, CandidateBlockResult)> {
		// the actual minimum page count needs to be a multiple of an order
		min_page_count = page_count_of_order(order_of_page_count_ceil(min_page_count));

		let end_addr = if in_higher_half {
			self.next_higher_half_top
		} else {
			self.next_lower_half_top
		};
		let mut start_addr = end_addr;
		start_addr -= min_page_count * PAGE_SIZE;
		start_addr = align_down_pow2(start_addr, 1u64 << alignment_power);

		// TODO: make sure the memory doesn't lie in a reserved L4 region

		let mut page_count = (end_addr - start_addr) / PAGE_SIZE;
		let bookkeeping_pages = (page_count.saturating_sub(RegionHeader::BITMAP_ENTRIES)
			+ ((PAGE_SIZE * 8) - 1))
			/ (PAGE_SIZE * 8);

		let header_mem = PhysicalFrame::allocate(bookkeeping_pages + 1).ok()?;
		let header_ptr = header_mem.address().as_mut_ptr::<RegionHeader>();
		// SAFETY: we just allocated this, so this is valid.
		let header_uninit =
			unsafe { header_ptr.as_uninit_mut() }.expect("header pointer should not be null");

		let header_val = RegionHeader {
			link: Default::default(),
			inner: UnsafeCell::new(InnerRegionHeader {
				page_count,
				virt_start_addr: start_addr,
				buckets: Default::default(),
				// SAFETY: this bitmap starts right after the end of the region header and extends to all the pages we reserved for it.
				//         additionally, alignment is not an issue because the `u8`s have single byte alignment.
				bitmap: unsafe {
					core::slice::from_raw_parts_mut(
						((header_ptr as u64) + size_of::<RegionHeader>() as u64) as *mut u8,
						(RegionHeader::BITMAP_SPACE + bookkeeping_pages * PAGE_SIZE) as usize,
					)
				},
			}),
		};

		header_uninit.write(header_val);
		// SAFETY: we just initialized it above
		let header = unsafe { header_uninit.assume_init_mut() };
		let inner_header = header.inner.get_mut();

		// clear out the bitmap
		inner_header.bitmap.fill(0);

		let mut block_addr = inner_header.virt_start_addr;
		while page_count > 0 {
			let order = order_of_page_count_floor(page_count);
			let pages = page_count_of_order(order);

			// SAFETY: we are inserting non-overlapping free blocks from within the region
			unsafe { inner_header.insert_free_block(block_addr, order) };

			block_addr += pages * PAGE_SIZE;
			page_count -= pages;
		}

		let mut _discard: Option<usize> = None;
		let candidate = inner_header.find_candidate_block(
			order_of_page_count_ceil(min_page_count),
			alignment_power,
			&mut _discard,
		);

		if in_higher_half {
			self.next_higher_half_top = start_addr;
			&mut self.higher_half
		} else {
			self.next_lower_half_top = start_addr;
			&mut self.lower_half
		}
		.push_back(header);

		Some((
			header,
			candidate.expect("freshly allocate region *must* contain minimum page count region"),
		))
	}

	fn find_region(
		&mut self,
		min_page_count: u64,
		alignment_power: u8,
		in_higher_half: bool,
	) -> Option<(&RegionHeader, CandidateBlockResult)> {
		let min_order = order_of_page_count_ceil(min_page_count);
		let regions = if in_higher_half {
			&mut self.higher_half
		} else {
			&mut self.lower_half
		};

		let mut candidate_order = None;

		// first, look for the smallest usable block from any region
		let result = regions
			.iter()
			.filter_map(|phys_region| {
				// SAFETY: we have exclusive access to the linked list, so this can't possibly be aliased.
				let inner = unsafe { phys_region.inner() };

				inner
					.find_candidate_block(min_order, alignment_power, &mut candidate_order)
					.map(|result| (phys_region, result))
			})
			.min_by_key(|item| item.1.order());

		result
	}

	fn find_or_allocate_region(
		&mut self,
		min_page_count: u64,
		alignment_power: u8,
		in_higher_half: bool,
	) -> Option<(&RegionHeader, CandidateBlockResult)> {
		if let Some((region, candidate)) =
			self.find_region(min_page_count, alignment_power, in_higher_half)
		{
			// SAFETY: this is necessary because the borrow checker doesn't currently understand that returning
			//         the value means that `self` is no longer mutable borrowed at the end of this conditional.
			//         we're effectively decoupling the lifetime of the region borrow from self, but it's re-bound
			//         to the self lifetime upon return.
			return Some((unsafe { &*(region as *const RegionHeader) }, candidate));
		}
		self.allocate_region(min_page_count, alignment_power, in_higher_half)
	}
}

impl Drop for InnerAddressSpace {
	fn drop(&mut self) {
		todo!()
	}
}

pub struct AddressSpace {
	inner: SpinLock<InnerAddressSpace>,
}

impl AddressSpace {
	pub fn new() -> Self {
		Self {
			inner: SpinLock::new(InnerAddressSpace::new()),
		}
	}
}
