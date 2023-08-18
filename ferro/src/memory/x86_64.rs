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

use core::arch::asm;

use bitflags::bitflags;

use super::PageTable;

bitflags! {
	pub(super) struct EntryFlags: u64 {
		const PRESENT = 1 << 0;
		const WRITABLE = 1 << 1;
		const ALLOW_UNPRIVILEGED_ACCESS = 1 << 2;
		const WRITE_THROUGH = 1 << 3;
		const NO_CACHE = 1 << 4;
		const ACCESSED = 1 << 5;
		const DIRTY = 1 << 6;
		const HUGE = 1 << 7;
		const GLOBAL = 1 << 8;
		const NX = 1 << 63;
	}
}

pub(super) const PHYSICAL_PAGE_MASK: u64 = 0xffffffffff << 12;

/// Retrieves a physical pointer to the current root (L4) page table.
///
/// # Safety
///
/// This operation is unsafe because of aliasing; Rust assumes it is the only one accessing the table.
pub(super) unsafe fn root_page_table_pointer_phys() -> *mut PageTable {
	let mut l4: *mut PageTable;
	asm!("mov {}, cr3", out(reg) l4, options(nostack, preserves_flags));
	l4
}

/// Reloads the root page table.
///
/// # Safety
///
/// This operation is unsafe because it cannot run when interrupts are enabled.
pub(super) unsafe fn refresh_root_page_table() {
	asm!(
		"mov {0}, cr3",
		"mov cr3, {0}",
		out(reg) _,
		options(nostack, preserves_flags)
	);
}

#[derive(Clone, Copy)]
#[repr(C)]
#[derive(Debug)]
pub(super) struct Entry(u64);

impl Default for Entry {
	fn default() -> Self {
		Self::new()
	}
}

pub(super) use super::common::EntryType;

impl Entry {
	pub const fn new() -> Self {
		Self(0)
	}

	pub const fn new_from_value(value: u64) -> Self {
		Self(value)
	}

	/// By default, the entry is present, unwritable, cacheable (write-back), and privileged.
	pub const fn new_from_address(physical_address: u64, entry_type: EntryType) -> Self {
		let flags = EntryFlags::PRESENT
			.union(match entry_type {
				EntryType::Large | EntryType::VeryLarge => EntryFlags::HUGE,
				_ => EntryFlags::empty(),
			})
			.bits();
		Self(flags | (physical_address & PHYSICAL_PAGE_MASK))
	}

	pub const fn as_writable(self, writable: bool) -> Self {
		Self(
			(self.0 & !EntryFlags::WRITABLE.bits())
				| (if writable {
					EntryFlags::WRITABLE
				} else {
					EntryFlags::empty()
				})
				.bits(),
		)
	}

	pub const fn as_cacheable(self, cacheable: bool) -> Self {
		Self(
			(self.0 & !EntryFlags::NO_CACHE.bits())
				| (if cacheable {
					EntryFlags::empty()
				} else {
					EntryFlags::NO_CACHE
				})
				.bits(),
		)
	}

	pub const fn as_present(self, present: bool) -> Self {
		Self(
			(self.0 & !EntryFlags::PRESENT.bits())
				| (if present {
					EntryFlags::PRESENT
				} else {
					EntryFlags::empty()
				})
				.bits(),
		)
	}

	pub const fn as_privileged(self, privileged: bool) -> Self {
		Self(
			(self.0 & !EntryFlags::ALLOW_UNPRIVILEGED_ACCESS.bits())
				| (if privileged {
					EntryFlags::empty()
				} else {
					EntryFlags::ALLOW_UNPRIVILEGED_ACCESS
				})
				.bits(),
		)
	}

	pub const fn as_value(self) -> u64 {
		self.0
	}

	pub const fn address(self) -> u64 {
		self.0 & PHYSICAL_PAGE_MASK
	}

	pub const fn entry_type(self, level: u8) -> EntryType {
		match level {
			1 => EntryType::Regular,
			2 => {
				if (self.0 & EntryFlags::HUGE.bits()) != 0 {
					EntryType::Large
				} else {
					EntryType::Table
				}
			},
			3 => {
				if (self.0 & EntryFlags::HUGE.bits()) != 0 {
					EntryType::VeryLarge
				} else {
					EntryType::Table
				}
			},
			4 => EntryType::Table,
			_ => panic!("Invalid level"),
		}
	}
}

pub(super) fn synchronize_after_table_modification() {
	// not necessary on this architecture
}
