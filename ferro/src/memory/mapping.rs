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
	pslab::{IntrusivePSlabAllocation, PSlab, PSlabPointerOps, PSlabRegion},
	PhysicalAddress,
};
use crate::{custom_intrusive_adapter, sync::SpinLock};

static MAPPING_SLAB: ArcFramePSlab<InnerMapping> = ArcFramePSlab::new();
static MAPPING_PORTION_SLAB: PSlab<MappingPortion> = PSlab::new();

bitflags! {
	struct MappingFlags: u64 {
		/// Zero-out pages allocated on-demand for this mapping.
		const ZERO = 1 << 0;
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

// TODO: WORKING HERE

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
pub struct InnerMapping {
	flags: MappingFlags,
	portions: SpinLock<LinkedList<MappingPortionAdapter>>,
}

// just here to prevent unexpected size changes
const_assert_eq!(size_of::<InnerMapping>(), 32);

#[derive(Clone)]
pub struct Mapping {
	inner: ArcFrame<InnerMapping>,
}
