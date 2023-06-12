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

mod ops;
mod range;
mod slice;

use core::marker::Destruct;

pub use ops::*;
pub use range::*;
pub use slice::*;

pub const fn decode_utf8_and_length(bytes: &[u8]) -> Option<(char, u8)> {
	if bytes.len() == 0 {
		return None;
	}

	let first_byte = bytes[0];
	let mut required_length: u8 = 1;
	let mut utf32_result: u32;

	if (first_byte & 0x80) != 0 {
		if (first_byte & 0x20) == 0 {
			required_length = 2;
			utf32_result = (first_byte & 0x1f) as u32;
		} else if (first_byte & 0x10) == 0 {
			required_length = 3;
			utf32_result = (first_byte & 0x0f) as u32;
		} else if (first_byte & 0x08) == 0 {
			required_length = 4;
			utf32_result = (first_byte & 0x07) as u32;
		} else {
			return None;
		}
	} else {
		utf32_result = first_byte as u32;
	}

	if bytes.len() < required_length as usize {
		return None;
	}

	let mut i: u8 = 1;
	while i < required_length {
		utf32_result <<= 6;
		utf32_result |= (bytes[i as usize] & 0x3f) as u32;
		i += 1;
	}

	Some((char::from_u32(utf32_result).unwrap(), required_length))
}

pub const fn decode_utf8(bytes: &[u8]) -> Option<char> {
	match decode_utf8_and_length(bytes) {
		Some(x) => Some(x.0),
		None => None,
	}
}

#[const_trait]
pub trait Alignable:
	~const ConstBitAnd<Output = Self>
	+ ~const ConstNot<Output = Self>
	+ ~const ConstAdd<Output = Self>
	+ ~const ConstSub<Output = Self>
	+ ~const ConstCountOnes
	+ Sized
	+ ~const ConstFrom<u8>
	+ Copy
{
}

impl<T> const Alignable for T where
	T: ~const ConstBitAnd<Output = Self>
		+ ~const ConstNot<Output = Self>
		+ ~const ConstAdd<Output = Self>
		+ ~const ConstSub<Output = Self>
		+ ~const ConstCountOnes
		+ Sized
		+ ~const ConstFrom<u8>
		+ Copy
{
}

pub const fn align_down_pow2<T>(value: T, alignment: T) -> T
where
	T: ~const Alignable,
	<T as ConstCountOnes>::Output: ~const ConstPartialEq + ~const ConstFrom<u8> + ~const Destruct,
{
	#[cfg(debug_assertions)]
	if alignment
		.const_count_ones()
		.const_ne(&<T as ConstCountOnes>::Output::const_from(1u8))
	{
		panic!("Invalid alignment");
	}

	value.const_bitand((alignment.const_sub(T::const_from(1u8))).const_not())
}

pub const fn align_up_pow2<T>(value: T, alignment: T) -> T
where
	T: ~const Alignable,
	<T as ConstCountOnes>::Output: ~const ConstPartialEq + ~const ConstFrom<u8> + ~const Destruct,
{
	align_down_pow2(
		value.const_add(alignment.const_sub(T::const_from(1u8))),
		alignment,
	)
}

/// Returns the closest power of 2 less than or equal to the given value.
///
/// Note that this returns `0` for `0`.
pub const fn closest_pow2_floor<T>(value: T) -> T
where
	T: ~const ConstILog2
		+ ~const ConstShl<<T as ConstILog2>::Output, Output = T>
		+ ~const ConstFrom<u8>
		+ ~const ConstPartialEq
		+ ~const Destruct,
{
	if value.const_eq(&T::const_from(0u8)) {
		T::const_from(0u8)
	} else {
		T::const_from(1u8).const_shl(value.const_ilog2())
	}
}

/// Returns the closest power of 2 greater than or equal to the given value.
///
/// In contrast to next_power_of_two, this returns `0` for `0`.
pub const fn closest_pow2_ceil<T>(value: T) -> T
where
	T: ~const ConstNextPowerOfTwo<Output = T>
		+ ~const ConstFrom<u8>
		+ ~const ConstPartialEq
		+ ~const Destruct,
{
	if value.const_eq(&T::const_from(0u8)) {
		T::const_from(0u8)
	} else {
		value.const_next_power_of_two()
	}
}
