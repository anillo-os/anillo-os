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
 * The main purpose of this file to provide `const_trait` versions of common operator traits from the standard library.
 */

#[const_trait]
pub trait ConstPartialEq<Rhs: ?Sized = Self> {
	fn const_eq(&self, other: &Rhs) -> bool;

	#[inline]
	fn const_ne(&self, other: &Rhs) -> bool {
		!self.const_eq(other)
	}
}

macro_rules! const_partial_eq_impl_auto {
	($($ty:ident),* $(,)?) => {
		$(
			impl const $crate::util::ConstPartialEq for $ty {
				fn const_eq(&self, other: &Self) -> bool {
					*self == *other
				}
			}
		)*
	};
}

const_partial_eq_impl_auto! {
	u8, u16, u32, u64, u128, usize,
	i8, i16, i32, i64, i128, isize,
	f32, f64, bool, char,
}

#[const_trait]
pub trait ConstDefault {
	fn const_default() -> Self;
}

macro_rules! const_default_impl_auto {
	($($target:ty),* $(,)?) => {
		$(
			impl const $crate::util::ConstDefault for $target {
				fn const_default() -> Self {
					0
				}
			}
		)*
	};
}

const_default_impl_auto! {
	u8, u16, u32, u64, u128, usize,
	i8, i16, i32, i64, i128, isize,
}

#[macro_export]
macro_rules! const_default_impl {
	($($target:ty => $body:block)*) => {
		$(
			impl const $crate::util::ConstDefault for $target {
				fn const_default() -> Self $body
			}

			impl ::core::default::Default for $target {
				fn default() -> Self $body
			}
		)*
	};
}

impl const ConstDefault for bool {
	fn const_default() -> Self {
		false
	}
}

impl const ConstDefault for char {
	fn const_default() -> Self {
		'\x00'
	}
}

impl const ConstDefault for f32 {
	fn const_default() -> Self {
		0.0f32
	}
}

impl const ConstDefault for f64 {
	fn const_default() -> Self {
		0.0f64
	}
}

#[const_trait]
pub trait ConstFrom<T>: Sized {
	fn const_from(value: T) -> Self;
}

#[const_trait]
pub trait ConstInto<T>: Sized {
	fn const_into(self) -> T;
}

// ConstFrom implies ConstInto in the opposite direction
impl<T, U: ~const ConstFrom<T>> const ConstInto<U> for T {
	#[inline]
	fn const_into(self) -> U {
		U::const_from(self)
	}
}

// ConstFrom on the same type is just that type
impl<T> const ConstFrom<T> for T {
	#[inline(always)]
	fn const_from(value: T) -> T {
		value
	}
}

#[macro_export]
macro_rules! const_from_impl {
	($($name:ident : $source:ty => $dest:ty $body:block)*) => {
		$(
			impl const $crate::util::ConstFrom<$source> for $dest {
				fn const_from($name : $source) -> Self $body
			}

			impl ::core::convert::From<$source> for $dest {
				fn from($name : $source) -> Self $body
			}
		)*
	};
}

macro_rules! const_from_widening_impl {
	($($source:ty => $dest:ty $(=> $other:ty)*),* $(,)?) => {
		$(
			impl const $crate::util::ConstFrom<$source> for $dest {
				fn const_from(value: $source) -> Self {
					value as $dest
				}
			}

			$(const_from_widening_impl! { $source => $other })*
			$(const_from_widening_impl! { $dest => $other })*
		)*
	};
}

const_from_widening_impl! {
	u8 => u16 => u32 => u64 => u128,
	i8 => i16 => i32 => i64 => i128,
}

macro_rules! const_operator_impl {
	($($ty:ty),+ $(,)? => $trait_name:ident < $rhs_type:ty > @ $output:ty $(, $default_output:ty)? : $func_name:ident ( $self:ident, $rhs_name:ident ) => $value:expr) => {
		$(
			impl const $trait_name<$rhs_type> for $ty {
				type Output = $output;

				fn $func_name($self, $rhs_name: $rhs_type) -> Self::Output {
					$value
				}
			}
		)+
	};
	($($ty:ty),+ $(,)? => $trait_name:ident @ $output:ty $(, $default_output:ty)? : $func_name:ident ( $self:ident ) => $value:expr) => {
		$(
			impl const $trait_name for $ty {
				type Output = $output;

				fn $func_name($self) -> Self::Output {
					$value
				}
			}
		)+
	};
}

macro_rules! const_operator_impl_type_class {
	(i $(, $other:ident)* => $($other_tt:tt)*) => {
		const_operator_impl! {
			i8, i16, i32, i64, i128, isize,
			=> $($other_tt)*
		}
		const_operator_impl_type_class! { $($other),* => $($other_tt)* }
	};
	(u $(, $other:ident)* => $($other_tt:tt)*) => {
		const_operator_impl! {
			u8, u16, u32, u64, u128, usize,
			=> $($other_tt)*
		}
		const_operator_impl_type_class! { $($other),* => $($other_tt)* }
	};
	(u_no_u32 $(, $other:ident)* => $($other_tt:tt)*) => {
		const_operator_impl! {
			u8, u16, u64, u128, usize,
			=> $($other_tt)*
		}
		const_operator_impl_type_class! { $($other),* => $($other_tt)* }
	};
	(f $(, $other:ident)* => $($other_tt:tt)*) => {
		const_operator_impl! {
			f32, f64,
			=> $($other_tt)*
		}
		const_operator_impl_type_class! { $($other),* => $($other_tt)* }
	};
	(b $(, $other:ident)* => $($other_tt:tt)*) => {
		const_operator_impl! {
			bool,
			=> $($other_tt)*
		}
		const_operator_impl_type_class! { $($other),* => $($other_tt)* }
	};
	(=> $($other_tt:tt)*) => {};
	() => {};
}

macro_rules! const_operator {
	($trait_name:ident [ $($type_class:ident),+ $(,)? ] : $func_name:ident ( $self:ident, $rhs_name:ident ) $(-> $output:ty)? => $value:expr $(, $other_trait_name:ident [ $($other_type_class:ident),+ $(,)? ] : $other_func_name:ident ( $other_self:ident $(, $other_rhs_name:ident)? ) $(-> $other_output:ty)? => $other_value:expr)* $(,)?) => {
		#[const_trait]
		pub trait $trait_name<Rhs = Self> {
			type Output;

			fn $func_name($self, $rhs_name: Rhs) -> Self::Output;
		}

		const_operator_impl_type_class! {
			$($type_class),+ => $trait_name<Self> @ $($output , )? Self: $func_name($self, $rhs_name) => $value
		}

		const_operator! { $($other_trait_name [ $($other_type_class),+ ] : $other_func_name ($other_self $(, $other_rhs_name)?) $(-> $other_output)? => $other_value),* }
	};
	($trait_name:ident [ $($type_class:ident),+ $(,)? ] : $func_name:ident ($self:ident) $(-> $output:ty)? => $value:expr $(, $other_trait_name:ident [ $($other_type_class:ident),+ $(,)? ] : $other_func_name:ident ( $other_self:ident $(, $other_rhs_name:ident)? ) $(-> $other_output:ty)? => $other_value:expr)* $(,)?) => {
		#[const_trait]
		pub trait $trait_name {
			type Output;

			fn $func_name($self) -> Self::Output;
		}

		const_operator_impl_type_class! {
			$($type_class),+ => $trait_name @ $($output , )? Self: $func_name($self) => $value
		}

		const_operator! { $($other_trait_name [ $($other_type_class),+ ] : $other_func_name ($other_self $(, $other_rhs_name)?) $(-> $other_output)? => $other_value),* }
	};
	() => {};
}

const_operator! {
	ConstAdd[i, u, f]: const_add(self, rhs) => self + rhs,
	ConstSub[i, u, f]: const_sub(self, rhs) => self - rhs,
	ConstMul[i, u, f]: const_mul(self, rhs) => self * rhs,
	ConstDiv[i, u, f]: const_div(self, rhs) => self / rhs,
	ConstRem[i, u, f]: const_rem(self, rhs) => self % rhs,
	ConstNeg[i, f]: const_neg(self) => -self,

	ConstNot[i, u, b]: const_not(self) => !self,
	ConstBitAnd[i, u, b]: const_bitand(self, rhs) => self & rhs,
	ConstBitOr[i, u, b]: const_bitor(self, rhs) => self | rhs,
	ConstBitXor[i, u, b]: const_bitxor(self, rhs) => self ^ rhs,

	ConstShl[i, u]: const_shl(self, rhs) => self << rhs,
	ConstShr[i, u]: const_shr(self, rhs) => self >> rhs,

	ConstCountOnes[i, u]: const_count_ones(self) -> u32 => self.count_ones(),
	ConstCountZeros[i, u]: const_count_zeros(self) -> u32 => self.count_zeros(),
	ConstILog2[i, u]: const_ilog2(self) -> u32 => self.ilog2(),
	ConstNextPowerOfTwo[u]: const_next_power_of_two(self) => self.next_power_of_two(),
}

const_operator_impl_type_class! {
	i, u_no_u32 => ConstShl<u32> @ Self: const_shl(self, rhs) => self << rhs
}

const_operator_impl_type_class! {
	i, u_no_u32 => ConstShr<u32> @ Self: const_shr(self, rhs) => self >> rhs
}
