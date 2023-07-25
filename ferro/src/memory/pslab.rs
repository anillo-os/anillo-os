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

//! This module implements a physical slab allocator for internal use.

use core::{
	marker::PhantomData,
	mem::{align_of, size_of, MaybeUninit},
	ops::{Deref, DerefMut},
	ptr::{addr_of_mut, null_mut, NonNull},
	sync::atomic::{fence, AtomicUsize, Ordering},
};

use intrusive_collections::{intrusive_adapter, LinkedList, LinkedListAtomicLink, PointerOps};

use super::{pmm::PhysicalFrame, PAGE_SIZE};
use crate::{
	sync::{Lock, SpinLock},
	util::ConstDefault,
};

struct FreeNode {
	next: *mut FreeNode,
}

pub(super) struct PSlabRegion {
	link: LinkedListAtomicLink,
	counter: AtomicUsize,
	frame: PhysicalFrame,
	// TODO: can we implement this in a lock-free manner?
	first_free: SpinLock<*mut FreeNode>,
}

intrusive_adapter!(PSlabRegionAdapter = &'static PSlabRegion: PSlabRegion { link: LinkedListAtomicLink });

pub(super) struct PSlabAllocation<T> {
	data: NonNull<T>,
	reference: PSlabRef<T>,
}

impl<T> PSlabAllocation<T> {
	pub(super) fn detach(self) -> (NonNull<T>, PSlabRef<T>) {
		// SAFETY: we read the structure to avoid cloning it, and then we
		//         immediately forget the stored copy in self below so that our read copy
		//         is the only valid copy.
		let result = (self.data, unsafe {
			(&self.reference as *const PSlabRef<T>).read()
		});
		core::mem::forget(self);
		result
	}

	/// SAFETY: `data` and `reference` must have been obtained from *the same call* to Self::detach
	///         on a previous PSlabAllocation<T> instance.
	pub(super) unsafe fn from_detached(data: NonNull<T>, reference: PSlabRef<T>) -> Self {
		Self { data, reference }
	}
}

impl<T> Deref for PSlabAllocation<T> {
	type Target = T;

	fn deref(&self) -> &Self::Target {
		// SAFETY: as long as we have the allocation, we can safely reference the data
		unsafe { self.data.as_ref() }
	}
}

impl<T> DerefMut for PSlabAllocation<T> {
	fn deref_mut(&mut self) -> &mut Self::Target {
		// SAFETY: PSlabAllocations are move-only types (unlike e.g. ArcFrame), so we can be sure that having a mutable reference
		//         to the allocation means that we have exclusive access to the data
		unsafe { self.data.as_mut() }
	}
}

impl<T> Drop for PSlabAllocation<T> {
	fn drop(&mut self) {
		// SAFETY(free): we know the data was allocated from this region
		// SAFETY(new_unchecked): we know the data pointer is not null
		unsafe {
			self.reference
				.region()
				.free(unsafe { NonNull::new_unchecked(self.data.as_ptr() as *mut MaybeUninit<T>) })
		}
	}
}

impl PSlabRegion {
	/// SAFETY: this PSlabRegion *must* be allocated for elements of the given type
	unsafe fn allocate<T>(&self) -> Option<NonNull<MaybeUninit<T>>> {
		let mut first_free_ptr = self.first_free.lock();
		let first_free = *first_free_ptr;
		if first_free == null_mut() {
			return None;
		}

		// SAFETY: we're holding the lock and we know that the free nodes in the PSlabRegion are valid
		let next_ptr = unsafe { (*first_free).next };
		unsafe { *first_free_ptr = next_ptr };

		// SAFETY: we just checked above that the first free pointer is not null
		Some(unsafe { NonNull::new_unchecked(first_free as *mut MaybeUninit<T>) })
	}

	unsafe fn free<T>(&self, ptr: NonNull<MaybeUninit<T>>) {
		let mut first_free_ptr = self.first_free.lock();
		let first_free = *first_free_ptr;

		// SAFETY: the caller has told us this is a valid node within this region
		let as_node = unsafe { (ptr.as_ptr() as *mut MaybeUninit<FreeNode>).as_mut() }
			.expect("pointer should not be null");
		as_node.write(FreeNode { next: first_free });

		// SAFETY: we just initialized this above
		*first_free_ptr = unsafe { as_node.assume_init_mut() };
	}
}

impl const ConstDefault for LinkedList<PSlabRegionAdapter> {
	fn const_default() -> Self {
		Self::new(PSlabRegionAdapter::NEW)
	}
}

/// A simple slab allocator for allocating groups of types in physical memory.
///
/// For our internal needs here in the memory subsystem, PSlabs **must** be static variables.
pub(super) struct PSlab<T> {
	regions: SpinLock<LinkedList<PSlabRegionAdapter>>,

	// just so that you can't accidentally mix and match PSlabs of different types
	phantom: PhantomData<T>,
}

impl<T> PSlab<T> {
	pub(super) const fn new() -> Self {
		Self {
			regions: ConstDefault::const_default(),
			phantom: PhantomData,
		}
	}

	fn new_region(&self) -> Option<PSlabRef<T>> {
		assert!(align_of::<T>() <= align_of::<PSlabRegion>());

		let entry_size = core::cmp::max(size_of::<T>() as u64, size_of::<FreeNode>() as u64);
		let entry_count = (PAGE_SIZE - (size_of::<PSlabRegion>() as u64)) / entry_size;
		assert!(entry_count > 0);

		let frame = PhysicalFrame::allocate(1).ok()?;
		let frame_addr = frame.address();
		let region_ptr = frame_addr.as_mut_ptr::<PSlabRegion>();
		// SAFETY: we just allocated this above
		let region_uninit =
			unsafe { region_ptr.as_uninit_mut() }.expect("region pointer should not be null");
		let first_addr = frame_addr.as_value() + (size_of::<PSlabRegion>() as u64);

		region_uninit.write(PSlabRegion {
			link: Default::default(),
			counter: AtomicUsize::new(1),
			frame,
			first_free: SpinLock::new(first_addr as *mut FreeNode),
		});

		for i in 0..entry_count {
			let node_addr = first_addr + (entry_size * i);
			let node_ptr = node_addr as *mut FreeNode;

			assert!(node_addr >= first_addr && node_addr < frame_addr.as_value() + PAGE_SIZE);

			// SAFETY: we know this is within the region because we just asserted it above
			let node_uninit =
				unsafe { node_ptr.as_uninit_mut() }.expect("node pointer should not be null");

			let next = if i + 1 == entry_count {
				null_mut()
			} else {
				(first_addr + (entry_size * (i + 1))) as *mut FreeNode
			};

			node_uninit.write(FreeNode { next });
		}

		// SAFETY: we just initialized this above
		let region = unsafe { region_uninit.assume_init_ref() };

		// add the region to the region list
		{
			let mut regions = self.regions.lock();
			regions.push_back(region);
		}

		// SAFETY: we just allocated the region above and gave ourselves a reference (by setting the counter equal to 1)
		Some(unsafe { PSlabRef::new_no_increment(region.into(), self as *const Self) })
	}

	fn find_region<F, R>(&self, mut f: F) -> Option<R>
	where
		F: FnMut(PSlabRef<T>) -> Option<R>,
	{
		let regions = self.regions.lock();
		for region in regions.iter() {
			// try to increment the counter, but leave it alone if it reaches 0
			// (that means the region is being destroyed)
			//
			// this is relaxed in every case because we don't really care about the ordering of increments/decrements
			// nor do we care to see the destruction of the region.
			let result = region
				.counter
				.fetch_update(Ordering::Relaxed, Ordering::Relaxed, |val| {
					if val == 0 {
						None
					} else {
						Some(val + 1)
					}
				});
			if result.is_err() {
				continue;
			}

			// SAFETY: we just incremented the counter above
			let reference =
				unsafe { PSlabRef::new_no_increment(region.into(), self as *const Self) };
			if let Some(result) = f(reference) {
				return Some(result);
			}
		}
		None
	}

	pub(super) fn allocate(&self, value: T) -> Option<PSlabAllocation<T>> {
		let allocator = |reference: PSlabRef<T>| {
			// SAFETY: this slab only refers to regions that allocate objects of type T
			unsafe { reference.region().allocate::<T>() }.map(|val| (val, reference))
		};
		self.find_region(allocator)
			.or_else(|| {
				let reference = self.new_region()?;
				allocator(reference)
			})
			.map(|(mut alloc, reference)| {
				// SAFETY: we just allocated this above
				let ptr = unsafe { alloc.as_mut() };
				ptr.write(value);
				PSlabAllocation {
					// SAFETY: we just initialized this above
					data: unsafe { ptr.assume_init_mut().into() },
					reference,
				}
			})
	}
}

// SAFETY: PSlabs are always safe to send and share across threads (as long as they're not moved)
unsafe impl<T> Send for PSlab<T> {}
unsafe impl<T> Sync for PSlab<T> {}

pub(super) struct PSlabRef<T>(NonNull<PSlabRegion>, *const PSlab<T>);

impl<T> PSlabRef<T> {
	pub(super) unsafe fn new_no_increment(
		region: NonNull<PSlabRegion>,
		pslab: *const PSlab<T>,
	) -> Self {
		// SAFETY: we're assuming that the caller knows that the region is alive
		Self(region, pslab)
	}

	pub(super) unsafe fn new(region: NonNull<PSlabRegion>, pslab: *const PSlab<T>) -> Self {
		// SAFETY: we're assuming that the caller knows that the region is alive
		unsafe { region.as_ref() }
			.counter
			.fetch_add(1, Ordering::Relaxed);
		Self(region, pslab)
	}

	fn region(&self) -> &PSlabRegion {
		// SAFETY: we own a reference on the data, so it's perfectly valid for us to dereference it
		unsafe { self.0.as_ref() }
	}

	fn slab(&self) -> &PSlab<T> {
		// SAFETY: slabs *must* be static
		//
		// TODO: figure out how to express this requirement without breaking the borrow checker
		unsafe { &*self.1 }
	}
}

impl<T> Clone for PSlabRef<T> {
	fn clone(&self) -> Self {
		self.region().counter.fetch_add(1, Ordering::Relaxed);

		Self(self.0, self.1)
	}
}

impl<T> Drop for PSlabRef<T> {
	fn drop(&mut self) {
		if self.region().counter.fetch_sub(1, Ordering::Release) != 1 {
			return;
		}

		// this fence synchronizes with the release above so that the thread that is destroying
		// the region (this thread) can see everything that happened until all the references were released.
		fence(Ordering::Acquire);

		// SAFETY: we know that no one else can possibly be referencing the inner data at this point
		//         because we're the ones destroying it.
		//
		// read the frame so we can drop it later
		let frame = unsafe { addr_of_mut!((*self.0.as_ptr()).frame).read() };

		// remove the region from the slab
		{
			let mut regions = self.slab().regions.lock();
			// SAFETY: we know this region is a part of the given slab and we're the only ones destroying it
			let mut cursor = unsafe { regions.cursor_mut_from_ptr(self.0.as_ptr()) };
			cursor.remove();
		}

		// now we can drop the backing memory
		// (explicitly drop it just to be clear)
		drop(frame)
	}
}

pub(super) trait IntrusivePSlabAllocation
where
	Self: Sized,
{
	fn slab() -> *const PSlab<Self>;
	fn region(&self) -> NonNull<PSlabRegion>;
}

pub(super) struct PSlabPointerOps<T: IntrusivePSlabAllocation>(PhantomData<T>);

impl<T: IntrusivePSlabAllocation> PSlabPointerOps<T> {
	pub(super) const fn new() -> Self {
		Self(PhantomData)
	}
}

impl<T: IntrusivePSlabAllocation> Clone for PSlabPointerOps<T> {
	fn clone(&self) -> Self {
		Self(PhantomData)
	}
}
impl<T: IntrusivePSlabAllocation> Copy for PSlabPointerOps<T> {}

unsafe impl<T: IntrusivePSlabAllocation> PointerOps for PSlabPointerOps<T> {
	type Pointer = PSlabAllocation<T>;
	type Value = T;

	#[inline]
	unsafe fn from_raw(&self, raw: *const T) -> PSlabAllocation<T> {
		PSlabAllocation::from_detached(
			NonNull::new_unchecked(raw as *mut T),
			PSlabRef::new_no_increment((*raw).region(), T::slab()),
		)
	}

	#[inline]
	fn into_raw(&self, ptr: PSlabAllocation<T>) -> *const T {
		let (data, reference) = ptr.detach();
		// forget the reference so that we hang on to it until we rebuild it later
		core::mem::forget(reference);
		data.as_ptr() as *const T
	}
}
