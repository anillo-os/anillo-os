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

use super::PAGE_SIZE;
use crate::util::{align_down_pow2, align_up_pow2};

pub const fn round_down_page(byte_size: u64) -> u64 {
	align_down_pow2(byte_size, PAGE_SIZE)
}

pub const fn round_up_page(byte_size: u64) -> u64 {
	align_up_pow2(byte_size, PAGE_SIZE)
}

pub const fn round_down_page_div(byte_size: u64) -> u64 {
	round_down_page(byte_size) / PAGE_SIZE
}

pub const fn round_up_page_div(byte_size: u64) -> u64 {
	round_up_page(byte_size) / PAGE_SIZE
}
