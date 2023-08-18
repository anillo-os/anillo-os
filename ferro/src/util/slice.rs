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
	mem::{ManuallyDrop, MaybeUninit},
	ops::Range,
};

//
// these would be much more ergonomic as const trait functions, but a recent nightly update broke that
// (and, in fact, const traits are being reworked, so they won't be available for a while).
//

pub const fn const_unroll<T, const N: usize>(slice: &[T]) -> [T; N]
where
	T: Copy,
{
	let mut tmp: [MaybeUninit<T>; N] = [MaybeUninit::uninit(); N];

	if slice.len() < tmp.len() {
		panic!("Invalid source (length less than unroll count)");
	}

	let mut i: usize = 0;
	while i < tmp.len() {
		tmp[i].write(slice[i]);
		i += 1;
	}

	// see https://github.com/rust-lang/rust/issues/61956
	unsafe { core::mem::transmute_copy(&ManuallyDrop::new(tmp)) }
}

pub const fn const_subslice<T>(slice: &[T], range: Range<usize>) -> &[T] {
	if range.start >= slice.len() {
		panic!("Invalid subslice start");
	}

	if range.end > slice.len() {
		panic!("Invalid subslice end");
	}

	let start = unsafe { slice.as_ptr().offset(range.start as isize) };

	unsafe { core::slice::from_raw_parts(start, range.end - range.start) }
}

pub const fn const_concat<T, const N: usize, const M: usize>(
	slice: &[T; N],
	other: &[T; M],
) -> [T; N + M]
where
	T: Copy,
{
	let mut tmp: [MaybeUninit<T>; N + M] = [MaybeUninit::uninit(); N + M];

	let mut i: usize = 0;
	while i < slice.len() {
		tmp[i].write(slice[i]);
		i += 1;
	}

	let mut j: usize = 0;
	while j < other.len() {
		tmp[i + j].write(other[j]);
		j += 1;
	}

	// see https://github.com/rust-lang/rust/issues/61956
	unsafe { core::mem::transmute_copy(&ManuallyDrop::new(tmp)) }
}
