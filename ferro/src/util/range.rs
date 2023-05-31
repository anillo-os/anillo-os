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

/*
 * The main purpose of this file to provide a `const_trait` version of the RangeBounds trait from the standard library.
 */

use core::ops::{Bound, Range, RangeFrom, RangeFull, RangeInclusive, RangeTo, RangeToInclusive};

#[const_trait]
pub trait ConstRangeBounds<T: ?Sized> {
	fn const_start_bound(&self) -> Bound<&T>;
	fn const_end_bound(&self) -> Bound<&T>;
}

impl<T> const ConstRangeBounds<T> for Range<T> {
	fn const_start_bound(&self) -> Bound<&T> {
		Bound::Included(&self.start)
	}

	fn const_end_bound(&self) -> Bound<&T> {
		Bound::Excluded(&self.end)
	}
}

impl<T> const ConstRangeBounds<T> for RangeInclusive<T> {
	fn const_start_bound(&self) -> Bound<&T> {
		Bound::Included(&self.start())
	}

	fn const_end_bound(&self) -> Bound<&T> {
		Bound::Included(&self.end())
	}
}

impl<T> const ConstRangeBounds<T> for RangeFrom<T> {
	fn const_start_bound(&self) -> Bound<&T> {
		Bound::Included(&self.start)
	}

	fn const_end_bound(&self) -> Bound<&T> {
		Bound::Unbounded
	}
}

impl<T> const ConstRangeBounds<T> for RangeTo<T> {
	fn const_start_bound(&self) -> Bound<&T> {
		Bound::Unbounded
	}

	fn const_end_bound(&self) -> Bound<&T> {
		Bound::Excluded(&self.end)
	}
}

impl<T> const ConstRangeBounds<T> for RangeToInclusive<T> {
	fn const_start_bound(&self) -> Bound<&T> {
		Bound::Unbounded
	}

	fn const_end_bound(&self) -> Bound<&T> {
		Bound::Included(&self.end)
	}
}

impl<T: ?Sized> const ConstRangeBounds<T> for RangeFull {
	fn const_start_bound(&self) -> Bound<&T> {
		Bound::Unbounded
	}

	fn const_end_bound(&self) -> Bound<&T> {
		Bound::Unbounded
	}
}
