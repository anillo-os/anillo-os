use core::arch::asm;

use bitflags::bitflags;

use super::PageTable;
use crate::const_default_impl;

bitflags! {
	pub(super) struct EntryFlags: u64 {
		const PRESENT = 1 << 0;
		/// For L1; indicates the entry contains a valid page.
		const VALID_PAGE = 1 << 1;
		/// For L2 and L3; indicates the entry points to a valid page table.
		const PAGE_TABLE_POINTER = 1 << 1;
		const NONSECURE = 1 << 5;
		const ALLOW_UNPRIVILEGED_ACCESS = 1 << 6;
		const NO_WRITE = 1 << 7;
		const ACCESSED = 1 << 10;
		const NOT_GLOBAL = 1 << 11;
		const NO_TRANSLATION = 1 << 16;
		const BTI_GUARDED = 1 << 50;
		const DIRTY = 1 << 51;
		const CONTIGUOUS = 1 << 52;
		const PRIVILEGED_NX = 1 << 53;
		const UNPRIVILEGED_NX = 1 << 54;

		const NX = Self::PRIVILEGED_NX.bits() | Self::UNPRIVILEGED_NX.bits();
	}
}

pub(super) const PHYSICAL_PAGE_MASK: u64 = 0xfffffffff << 12;

/// Retrieves a physical pointer to the current root (L4) page table.
///
/// # Safety
///
/// This operation is unsafe because of aliasing; Rust assumes it is the only one accessing the table.
pub(super) unsafe fn root_page_table_pointer_phys() -> *mut PageTable {
	let mut l4: *mut PageTable;
	asm!("mrs {}, ttbr0_el1", out(reg) l4, options(nostack, preserves_flags));
	l4
}

/// Reloads the root page table.
///
/// # Safety
///
/// This operation is unsafe because it cannot run when interrupts are enabled.
pub(super) unsafe fn refresh_root_page_table() {
	asm!(
		"mrs {0}, ttbr0_el1",
		"msr ttbr0_el1, {0}",
		out(reg) _,
		options(nostack, preserves_flags)
	);
}

#[derive(Clone, Copy)]
#[repr(C)]
#[derive(Debug)]
pub(super) struct Entry(u64);

const_default_impl! { Entry => {
	Entry(0)
}}

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
			.union(EntryFlags::ACCESSED)
			.union(EntryFlags::NO_WRITE)
			.union(match entry_type {
				EntryType::Regular => EntryFlags::VALID_PAGE,
				EntryType::Table => EntryFlags::PAGE_TABLE_POINTER,
				_ => EntryFlags::empty(),
			})
			.bits();
		Self(flags | (3 << 8) | (3 << 2) | (physical_address & PHYSICAL_PAGE_MASK))
	}

	pub const fn as_writable(self, writable: bool) -> Self {
		Self(
			(self.0 & !EntryFlags::NO_WRITE.bits())
				| (if writable {
					EntryFlags::empty()
				} else {
					EntryFlags::NO_WRITE
				})
				.bits(),
		)
	}

	pub const fn as_cacheable(self, _cacheable: bool) -> Self {
		// TODO
		self
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
				if (self.0 & EntryFlags::PAGE_TABLE_POINTER.bits()) != 0 {
					EntryType::Table
				} else {
					EntryType::Large
				}
			},
			3 => {
				if (self.0 & EntryFlags::PAGE_TABLE_POINTER.bits()) != 0 {
					EntryType::Table
				} else {
					EntryType::VeryLarge
				}
			},
			4 => EntryType::Table,
			_ => panic!("Invalid level"),
		}
	}
}

pub(super) fn synchronize_after_table_modification() {
	unimplemented!()
}
