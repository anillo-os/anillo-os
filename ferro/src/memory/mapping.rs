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

use core::{mem::size_of, ptr::NonNull};

use bitflags::bitflags;
use intrusive_collections::{LinkedList, LinkedListAtomicLink};
use static_assertions::const_assert_eq;

use super::{
	arc_frame::{ArcFrame, ArcFramePSlab},
	pmm::PhysicalFrame,
	pslab::{IntrusivePSlabAllocation, PSlab, PSlabPointerOps, PSlabRegion},
	PhysicalAddress,
};
use crate::{
	custom_intrusive_adapter,
	memory::PAGE_SIZE,
	sync::{Lock, SpinLock},
};

static MAPPING_SLAB: ArcFramePSlab<InnerMapping> = ArcFramePSlab::new();
static MAPPING_PORTION_SLAB: PSlab<MappingPortion> = PSlab::new();

bitflags! {
	pub struct MappingFlags: u64 {
		// TODO
	}
}

#[derive(Clone)]
enum MappingPortionBacking {
	OwnedFrame {
		start_addr: PhysicalAddress,
		total_page_count: u32,
		page_offset: u32,
		mapped_page_count: u32,
		portion_offset: u32,
	},
	UnownedFrame {
		start_addr: PhysicalAddress,
		page_count: u32,
		portion_offset: u32,
	},
	Mapping {
		mapping: Mapping,
		owner_offset: u32,
		page_count: u32,
		portion_offset: u32,
	},
}

impl MappingPortionBacking {
	fn page_count(&self) -> u32 {
		match self {
			Self::OwnedFrame {
				mapped_page_count, ..
			} => *mapped_page_count,
			Self::UnownedFrame { page_count, .. } => *page_count,
			Self::Mapping { page_count, .. } => *page_count,
		}
	}

	fn portion_offset(&self) -> u32 {
		match self {
			Self::OwnedFrame { portion_offset, .. } => *portion_offset,
			Self::UnownedFrame { portion_offset, .. } => *portion_offset,
			Self::Mapping { portion_offset, .. } => *portion_offset,
		}
	}
}

// keep this structure as small as possible!
pub(super) struct MappingPortion {
	link: LinkedListAtomicLink,
	region: NonNull<PSlabRegion>,
	backing: MappingPortionBacking,
}

// current size; this is just to make sure we keep it as small as possible and the size doesn't change unexpectedly
const_assert_eq!(size_of::<MappingPortion>(), 56);

impl IntrusivePSlabAllocation for MappingPortion {
	fn slab() -> *const PSlab<Self> {
		&MAPPING_PORTION_SLAB
	}

	fn region(&self) -> NonNull<PSlabRegion> {
		self.region
	}
}

custom_intrusive_adapter!(MappingPortionAdapter = PSlabPointerOps<MappingPortion>: MappingPortion { link: LinkedListAtomicLink });

impl Drop for MappingPortion {
	fn drop(&mut self) {
		todo!()
	}
}

// try to keep this structure small, but it's not nearly as crucial as MappingPortion
struct InnerMapping {
	page_count: u64,
	portions: SpinLock<LinkedList<MappingPortionAdapter>>,
}

// just here to prevent unexpected size changes
const_assert_eq!(size_of::<InnerMapping>(), 32);

#[derive(Clone)]
pub struct Mapping(ArcFrame<InnerMapping>);

pub enum BindError {
	/// An unknown error occurred.
	Unknown,

	/// Failed to allocate one or more frames or portions to complete the binding.
	AllocationFailure,

	/// The given page offset and count for the bind destination (i.e. the target portion of the mapping) is out-of-bounds.
	OutOfBoundsDestination,

	/// The given page offset and count for the bind source is out-of-bounds for the given frame.
	OutOfBoundsSource,

	/// The target portion has already been partially or completely bound.
	AlreadyBound,
}

impl Mapping {
	pub fn new(page_count: u64, flags: MappingFlags) -> Option<Self> {
		let inner = InnerMapping {
			page_count,
			portions: Default::default(),
		};
		Some(Self(ArcFrame::new_in_slab(inner, &MAPPING_SLAB)?))
	}

	pub fn page_count(&self) -> u64 {
		self.0.page_count
	}

	/// NOTE: This method assumes that all the necessary checks have already been
	///       performed.
	fn bind_internal(
		&self,
		portions: &mut LinkedList<MappingPortionAdapter>,
		backing: MappingPortionBacking,
	) -> Result<(), BindError> {
		match MAPPING_PORTION_SLAB.allocate_with(|reference, ptr| {
			ptr.write(MappingPortion {
				link: Default::default(),
				region: reference.region().into(),
				backing,
			})
		}) {
			Some(allocation) => {
				portions.push_back(allocation);
				Ok(())
			},
			None => Err(BindError::AllocationFailure),
		}
	}

	fn check_overlap(
		&self,
		portions: &LinkedList<MappingPortionAdapter>,
		bind_page_offset: u64,
		bind_page_count: u64,
	) -> Result<(), BindError> {
		if portions.iter().any(|portion| {
			let backing = &portion.backing;
			let backing_offset = backing.portion_offset() as u64;
			let backing_count = backing.page_count() as u64;
			// check if this portion contains the start of the tentative portion
			(
				backing_offset <= bind_page_offset &&
				(backing_offset + backing_count) > bind_page_offset
			) ||
			// check if the tentative portion contains the start of this portion
			(
				bind_page_offset <= backing_offset &&
				(bind_page_offset + bind_page_count) > backing_offset
			)
		}) {
			Err(BindError::AlreadyBound)
		} else {
			Ok(())
		}
	}

	/// Binds a portion of this mapping to one or more newly allocated pages.
	///
	/// In contrast to [`Self::bind_existing`] and [`Self::bind_indirect`],
	/// this method is safe. This is because there's no possibility to
	/// introduce aliasing because the memory being bound to the portion is newly
	/// allocated memory that isn't in-use for anything else.
	pub fn bind_new(
		&self,
		page_count: u64,
		page_offset: u64,
		zeroed: bool,
	) -> Result<(), BindError> {
		if page_offset + page_count > self.0.page_count {
			return Err(BindError::OutOfBoundsDestination);
		}

		if TryInto::<u32>::try_into(page_count).is_err() {
			panic!("arguments out-of-bounds");
		}

		// FIXME: we should *not* be holding the lock while allocating frames

		let mut portions = self.0.portions.lock();

		self.check_overlap(&portions, page_offset, page_count)?;

		for i in 0..page_count {
			match PhysicalFrame::allocate(1) {
				Ok(frame) => {
					if zeroed {
						// SAFETY: we know that the memory is valid because 1) we just allocated it and 2) it is one page long
						let bytes: &mut [u8] = unsafe {
							core::slice::from_raw_parts_mut(frame.address().as_mut_ptr(), PAGE_SIZE)
						};
						bytes.fill(0);
					}

					let (start_addr, page_count) = frame.detach();
					assert_eq!(page_count, 1);
					let backing = MappingPortionBacking::OwnedFrame {
						start_addr,
						total_page_count: 1,
						page_offset: 0,
						mapped_page_count: 1,
						portion_offset: (page_offset as u32) + i,
					};

					let result = self.bind_internal(&mut portions, backing);

					if result.is_err() {
						// abort the entire bind

						// SAFETY: we know this is a valid allocated frame because we just detached it above
						//         (and failing to bind it to a portion does not affect the frame allocation)
						drop(unsafe { PhysicalFrame::from_allocated(start_addr, 1) });

						// remove all the portions we added
						// (they should all be at the end of the list, since we just added them and we're still holding the lock)
						let mut cursor = portions.back_mut();
						for _ in 0..i {
							let item = cursor
								.remove()
								.expect("there must be a portion that we added");
							cursor.move_prev(); // move back from the null object
							drop(item); // explicitly drop the portion
						}

						return result;
					}
				},
				Err(()) => {
					// remove all the portions we added
					// (they should all be at the end of the list, since we just added them and we're still holding the lock)
					let mut cursor = portions.back_mut();
					for _ in 0..i {
						let item = cursor
							.remove()
							.expect("there must be a portion that we added");
						cursor.move_prev(); // move back from the null object
						drop(item); // explicitly drop the portion
					}

					return Err(BindError::AllocationFailure);
				},
			}
		}

		Ok(())
	}

	/// Binds a portion of this mapping to the given frame (either a portion of it or the whole thing).
	///
	/// Note that the mapping will take ownership of the given frame. If the frame owns the referenced memory,
	/// it will be deallocated once the mapping no longer needs it; otherwise (if the frame does *not* own the referenced memory),
	/// it will simply be discarded once the mapping no longer needs it.
	///
	/// # Safety
	///
	/// This method is unsafe because it may introduce aliasing by allowing the same memory
	/// to be referenced by multiple mappings. If this mapping is not mapped in any address space,
	/// there is no such issue. However, because it's possible to bind memory to mappings after
	/// they've already been mapped (and future lookups would return the newly bound memory),
	/// it's possible to introduce aliasing with it even if the initial mapping was perfectly safe
	/// (i.e. introduced no aliasing).
	pub unsafe fn bind_existing(
		&self,
		page_count: u64,
		bind_page_offset: u64,
		incoming_page_offset: u64,
		frame: PhysicalFrame,
	) -> Result<(), BindError> {
		if bind_page_offset + page_count > self.0.page_count {
			return Err(BindError::OutOfBoundsDestination);
		}
		if incoming_page_offset + page_count > frame.page_count() {
			return Err(BindError::OutOfBoundsSource);
		}

		if TryInto::<u32>::try_into(page_count).is_err()
			|| TryInto::<u32>::try_into(bind_page_offset).is_err()
			|| TryInto::<u32>::try_into(incoming_page_offset).is_err()
		{
			panic!("arguments out-of-bounds");
		}

		let mut portions = self.0.portions.lock();

		self.check_overlap(&portions, bind_page_offset, page_count)?;

		let owned = frame.owned();
		let (start_addr, total_page_count) = frame.detach();

		if TryInto::<u32>::try_into(total_page_count).is_err() {
			panic!("total page count out-of-bounds");
		}

		let backing = match owned {
			true => MappingPortionBacking::OwnedFrame {
				start_addr,
				total_page_count: total_page_count as u32,
				page_offset: incoming_page_offset as u32,
				mapped_page_count: page_count as u32,
				portion_offset: bind_page_offset as u32,
			},
			false => MappingPortionBacking::UnownedFrame {
				start_addr: start_addr.offset_pages(incoming_page_offset as i64),
				page_count: page_count as u32,
				portion_offset: bind_page_offset as u32,
			},
		};

		let result = self.bind_internal(&mut portions, backing);

		if result.is_err() && owned {
			// we failed to bind the portion (most likely: we failed to allocate the memory for the info structure).
			// this frame was owned (i.e. allocated), but the caller passes ownership of it to us unconditionally,
			// so we have to clean it up (i.e. deallocate it).

			// SAFETY: we know this is a valid allocated frame because we just detached it above
			//         (and failing to bind it to a portion does not affect the frame allocation)
			drop(unsafe { PhysicalFrame::from_allocated(start_addr, total_page_count) });
		}

		result
	}

	/// Binds a portion of this mapping to a portion of the given mapping.
	///
	/// This function operates in much the same way as [`Self::bind_existing()`], except that it will bind to the given mapping rather than a frame.
	/// This is useful, for example, because it allows delaying the actual binding of physical memory until later when it is needed.
	///
	/// Effectively, this allows mappings to share memory without having the memory bound yet.
	///
	/// # Safety
	///
	/// The safety considerations for this method are the same as for [`Self::bind_existing()`].
	/// However, this method is even *more* likely to introduce aliasing: because you're binding a
	/// portion of a mapping to portion of another mapping, it's even more likely that both mappings
	/// be in-use and thus the memory the reference will be aliased.
	pub unsafe fn bind_indirect(
		&self,
		page_count: u64,
		bind_page_offset: u64,
		incoming_page_offset: u64,
		mapping: Mapping,
	) -> Result<(), BindError> {
		if bind_page_offset + page_count > self.0.page_count {
			return Err(BindError::OutOfBoundsDestination);
		}
		if incoming_page_offset + page_count > mapping.page_count() {
			return Err(BindError::OutOfBoundsSource);
		}

		if TryInto::<u32>::try_into(page_count).is_err()
			|| TryInto::<u32>::try_into(bind_page_offset).is_err()
			|| TryInto::<u32>::try_into(incoming_page_offset).is_err()
		{
			panic!("arguments out-of-bounds");
		}

		let mut portions = self.0.portions.lock();

		self.check_overlap(&portions, bind_page_offset, page_count)?;

		let backing = MappingPortionBacking::Mapping {
			mapping,
			owner_offset: incoming_page_offset as u32,
			page_count: page_count as u32,
			portion_offset: bind_page_offset as u32,
		};

		self.bind_internal(&mut portions, backing)
	}
}
