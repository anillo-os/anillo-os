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
	order::MAX_ORDER,
	pmm::PhysicalFrame,
	region::{BuddyRegionHeader, FreeBlock, FreeBlockAdapter},
	PageTable, PhysicalAddress, KERNEL_L4_START, PAGE_SIZE, PHYSICAL_MEMORY_L4_INDEX,
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

impl RegionHeader {
	const BITMAP_SPACE: u64 = PAGE_SIZE - (size_of::<RegionHeader>() as u64);
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
	lower_half: LinkedList<RegionHeaderAdapter>,
}

impl InnerAddressSpace {
	/// The default size (in bytes) for new regions allocated in address spaces.
	///
	/// This value is derived from the leftover space in a frame after reserving the beginning for the region header.
	/// Each leftover byte is part of a bitmap that where a byte can represent 8 blocks and each block is the size of a page.
	/// This value is then truncated to be a power of 2.
	///
	/// On platforms where frames are 4 KiB wide, the initial value is 110.5 MiB, which is then truncated to 64 MiB.
	/// On platforms where frames are 16 KiB wide, the initial value is 1.9 GiB, which is then truncated to 1 GiB.
	/// On platforms where frames are 64 KiB wide, the initial value is 31.7 GiB, which is then truncated to 16 GiB.
	const DEFAULT_REGION_SIZE: u64 =
		closest_pow2_floor((RegionHeader::BITMAP_SPACE * 8) * PAGE_SIZE);

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
			lower_half: LinkedList::new(RegionHeaderAdapter::NEW),
		}
	}
}

impl Drop for InnerAddressSpace {
	fn drop(&mut self) {
		unimplemented!()
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
