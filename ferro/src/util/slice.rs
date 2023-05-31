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

use core::{marker::Destruct, ops::Bound};

use super::{ConstDefault, ConstPartialEq, ConstRangeBounds};

pub const fn slices_are_equal<T: ~const ConstPartialEq>(a: &[T], b: &[T]) -> bool {
	if a.len() != b.len() {
		return false;
	}
	let mut i: usize = 0;
	while i < a.len() {
		if a[i].const_ne(&b[i]) {
			return false;
		}
		i += 1;
	}
	return true;
}

#[const_trait]
pub trait ConstSlice<T> {
	fn const_copy_from_slice(&mut self, source: &Self)
	where
		T: Copy;

	fn const_unroll<const N: usize>(&self) -> [T; N]
	where
		T: Copy + ~const ConstDefault;

	fn const_subslice<R: ~const ConstRangeBounds<usize> + ~const Destruct>(&self, range: R)
		-> &[T];
	fn const_subslice_mut<R: ~const ConstRangeBounds<usize> + ~const Destruct>(
		&mut self,
		range: R,
	) -> &mut [T];
}

#[const_trait]
pub trait ConstSizedSlice<T, const N: usize> {
	fn const_concat<const M: usize>(&self, other: &[T; M]) -> [T; N + M]
	where
		T: Copy + ~const ConstDefault;
}

impl<T> const ConstSlice<T> for [T] {
	fn const_copy_from_slice(&mut self, source: &Self)
	where
		T: Copy,
	{
		if self.len() != source.len() {
			panic!("Invalid source (length not equal to destination length)");
		}

		unsafe {
			core::ptr::copy_nonoverlapping(source.as_ptr(), self.as_mut_ptr(), self.len());
		}
	}

	fn const_unroll<const N: usize>(&self) -> [T; N]
	where
		T: Copy + ~const ConstDefault,
	{
		let mut tmp: [T; N] = [ConstDefault::const_default(); N];

		if self.len() < tmp.len() {
			panic!("Invalid source (length less than unroll count)");
		}

		let mut i: usize = 0;
		while i < tmp.len() {
			tmp[i] = self[i];
			i += 1;
		}

		tmp
	}

	fn const_subslice<R: ~const ConstRangeBounds<usize> + ~const Destruct>(
		&self,
		range: R,
	) -> &[T] {
		let start_index = match range.const_start_bound() {
			Bound::Included(&x) => x,
			Bound::Excluded(&x) => x + 1,
			Bound::Unbounded => 0,
		};

		if start_index >= self.len() {
			panic!("Invalid subslice start");
		}

		let end_index = match range.const_end_bound() {
			Bound::Included(&x) => x + 1,
			Bound::Excluded(&x) => x,
			Bound::Unbounded => self.len(),
		};

		if end_index > self.len() {
			panic!("Invalid subslice end");
		}

		let start = unsafe { self.as_ptr().offset(start_index as isize) };

		unsafe { core::slice::from_raw_parts(start, end_index - start_index) }
	}

	fn const_subslice_mut<R: ~const ConstRangeBounds<usize> + ~const Destruct>(
		&mut self,
		range: R,
	) -> &mut [T] {
		let start_index = match range.const_start_bound() {
			Bound::Included(&x) => x,
			Bound::Excluded(&x) => x + 1,
			Bound::Unbounded => 0,
		};

		if start_index >= self.len() {
			panic!("Invalid subslice start");
		}

		let end_index = match range.const_end_bound() {
			Bound::Included(&x) => x + 1,
			Bound::Excluded(&x) => x,
			Bound::Unbounded => self.len(),
		};

		if end_index > self.len() {
			panic!("Invalid subslice end");
		}

		let start = unsafe { self.as_mut_ptr().offset(start_index as isize) };

		unsafe { core::slice::from_raw_parts_mut(start, end_index - start_index) }
	}
}

impl<T, const N: usize> const ConstSizedSlice<T, N> for [T; N] {
	fn const_concat<const M: usize>(&self, other: &[T; M]) -> [T; N + M]
	where
		T: Copy + ~const ConstDefault,
	{
		let mut tmp: [T; N + M] = [ConstDefault::const_default(); N + M];

		let mut i: usize = 0;
		while i < self.len() {
			tmp[i] = self[i];
			i += 1;
		}

		let mut j: usize = 0;
		while j < other.len() {
			tmp[i + j] = other[j];
			j += 1;
		}

		tmp
	}
}
