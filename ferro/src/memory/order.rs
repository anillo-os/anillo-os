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

pub(super) const MAX_ORDER: usize = 32;

/// NOTE: This function truncates the page count if necessary and returns the *maximum* order of the given page count.
pub(super) const fn order_of_page_count_floor(page_count: u64) -> usize {
	if page_count == 0 {
		usize::MAX
	} else {
		let order = page_count.ilog2() as usize;
		if order >= MAX_ORDER {
			MAX_ORDER - 1
		} else {
			order
		}
	}
}

pub(super) const fn order_of_page_count_ceil(page_count: u64) -> usize {
	let order = order_of_page_count_floor(page_count);
	if order != usize::MAX && page_count > page_count_of_order(order) {
		if order >= MAX_ORDER - 1 {
			usize::MAX
		} else {
			order + 1
		}
	} else {
		order
	}
}

pub(super) const fn page_count_of_order(order: usize) -> u64 {
	1u64 << (order as u64)
}

pub(super) const fn byte_count_of_order(order: usize) -> u64 {
	page_count_of_order(order) * PAGE_SIZE
}
