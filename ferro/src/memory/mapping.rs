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
use crate::{custom_intrusive_adapter, sync::SpinLock};

static MAPPING_SLAB: ArcFramePSlab<InnerMapping> = ArcFramePSlab::new();
static MAPPING_PORTION_SLAB: PSlab<MappingPortion> = PSlab::new();

bitflags! {
	struct MappingFlags: u64 {
		// TODO
	}
}

enum MappingPortionBacking {
	OwnedFrame(PhysicalAddress),
	UnownedFrame(PhysicalAddress),
	Mapping { mapping: Mapping, owner_offset: u32 },
}

// keep this structure as small as possible!
pub(super) struct MappingPortion {
	link: LinkedListAtomicLink,
	region: NonNull<PSlabRegion>,
	backing: MappingPortionBacking,
	page_count: u64,
}

// current size; this is just to make sure we keep it as small as possible and the size doesn't change unexpectedly
const_assert_eq!(size_of::<MappingPortion>(), 48);

impl IntrusivePSlabAllocation for MappingPortion {
	fn slab() -> *const PSlab<Self> {
		&MAPPING_PORTION_SLAB
	}

	fn region(&self) -> NonNull<PSlabRegion> {
		self.region
	}
}

custom_intrusive_adapter!(MappingPortionAdapter = PSlabPointerOps<MappingPortion>: MappingPortion { link: LinkedListAtomicLink });

// try to keep this structure small, but it's not nearly as crucial as MappingPortion
struct InnerMapping {
	portions: SpinLock<LinkedList<MappingPortionAdapter>>,
}

// just here to prevent unexpected size changes
const_assert_eq!(size_of::<InnerMapping>(), 32);

#[derive(Clone)]
pub struct Mapping(ArcFrame<InnerMapping>);

pub enum BindError {
	/// An unknown error occurred.
	Unknown,

	/// Failed to allocate one or more frames to complete the binding.
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
			portions: Default::default(),
		};
		Some(Self(ArcFrame::new_in_slab(inner, MAPPING_SLAB)?))
	}

	/// Binds a portion of this mapping to one or more newly allocated pages.
	pub fn bind_new(
		&self,
		page_count: u64,
		page_offset: u64,
		zeroed: bool,
	) -> Result<(), BindError> {
		todo!()
	}

	/// Binds a portion of this mapping to the given frame (either a portion of it or the whole thing).
	///
	/// Note that the mapping will take ownership of the given frame. If the frame owns the referenced memory,
	/// it will be deallocated once the mapping no longer needs it; otherwise (if the frame does *not* own the referenced memory),
	/// it will simply be discarded once the mapping no longer needs it.
	pub fn bind_existing(
		&self,
		page_count: u64,
		bind_page_offset: u64,
		incoming_page_offset: u64,
		frame: PhysicalFrame,
	) -> Result<(), BindError> {
		todo!()
	}

	/// Binds a portion of this mapping to a portion of the given mapping.
	///
	/// This function operates in much the same way as [`Self::bind_existing()`], except that it will bind to the given mapping rather than a frame.
	/// This is useful, for example, because it allows delaying the actual binding of physical memory until later when it is needed.
	///
	/// Effectively, this allows mappings to share memory without having the memory bound yet.
	pub fn bind_indirect(
		&self,
		page_count: u64,
		bind_page_offset: u64,
		incoming_page_offset: u64,
		mapping: Mapping,
	) -> Result<(), BindError> {
		todo!()
	}
}
