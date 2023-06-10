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

use core::mem::{align_of, size_of};

use static_assertions::const_assert;

use super::{pmm::PhysicalFrame, PageTable};
use crate::{memory::PAGE_SIZE, sync::SpinLock, util::align_down_pow2};

struct InnerAddressSpace {
	table_frame: PhysicalFrame,
}

impl InnerAddressSpace {
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
}

pub struct AddressSpace {
	inner: SpinLock<InnerAddressSpace>,
}

impl AddressSpace {
	pub fn new() -> Self {
		unimplemented!()
	}
}

impl Drop for AddressSpace {
	fn drop(&mut self) {
		unimplemented!()
	}
}
