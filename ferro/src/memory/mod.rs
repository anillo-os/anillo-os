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

use core::fmt::Debug;

use crate::{
	const_from_impl,
	util::{ConstDefault, ConstFrom, ConstInto},
	KernelImageInfo, MemoryRegion,
};

pub mod pmm;
pub mod vmm;

mod order;
mod util;

mod arc_frame;
mod common;
mod region;

mod custom_adapter;
pub mod mapping;
mod pslab;
#[cfg(target_arch = "x86_64")]
mod x86_64;
#[cfg(target_arch = "x86_64")]
use x86_64 as arch;

#[cfg(target_arch = "aarch64")]
mod aarch64;
#[cfg(target_arch = "aarch64")]
use aarch64 as arch;

use self::arch::{root_page_table_pointer_phys, EntryType};

#[repr(C, align(4096))]
struct PageTable {
	entries: [arch::Entry; 512],
}

pub const PAGE_SIZE: u64 = 0x1000;
pub const LARGE_PAGE_SIZE: u64 = 0x20_0000;
pub const VERY_LARGE_PAGE_SIZE: u64 = 0x4000_0000;

pub const KERNEL_VIRTUAL_START: u64 = 0xffff_8000_0000_0000;
const KERNEL_L4_START: usize = DecomposedAddress::const_from(KERNEL_VIRTUAL_START).l4 as usize;
const PHYSICAL_MEMORY_L4_INDEX: usize = 511;

pub const L1_SHIFT: u64 = 12;
pub const L2_SHIFT: u64 = 21;
pub const L3_SHIFT: u64 = 30;
pub const L4_SHIFT: u64 = 39;

const PHYSICAL_MAPPED_BASE: u64 = make_virtual_address(PHYSICAL_MEMORY_L4_INDEX as u16, 0, 0, 0, 0);

/// A pointer that represents a physical address.
#[derive(Clone, Copy, PartialEq, Eq)]
pub struct PhysicalAddress(u64);

impl PhysicalAddress {
	fn new(value: u64) -> Self {
		Self(value)
	}

	fn as_value(&self) -> u64 {
		self.0
	}

	fn as_ptr<T>(&self) -> *const T {
		(self.0 + PHYSICAL_MAPPED_BASE) as *const T
	}

	fn as_mut_ptr<T>(&self) -> *mut T {
		(self.0 + PHYSICAL_MAPPED_BASE) as *mut T
	}

	fn offset_pages(&self, page_count: i64) -> Self {
		Self(self.0.wrapping_add_signed(page_count * (PAGE_SIZE as i64)))
	}
}

// we implement Debug and not Display because you should only ever see physical addresses during debugging
impl Debug for PhysicalAddress {
	fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
		write!(f, "PhysicalAddress({:#x})", self.0)
	}
}

struct DecomposedAddress {
	l4: u16,
	l3: u16,
	l2: u16,
	l1: u16,
	offset: u16,
}

const_from_impl! { addr: u64 => DecomposedAddress {
	DecomposedAddress {
		l4: ((addr >> L4_SHIFT) & 0x1ff) as u16,
		l3: ((addr >> L3_SHIFT) & 0x1ff) as u16,
		l2: ((addr >> L2_SHIFT) & 0x1ff) as u16,
		l1: ((addr >> L1_SHIFT) & 0x1ff) as u16,
		offset: (addr & 0x1ff) as u16,
	}
}}

pub const PAGE_TABLE_INDEX_MAX: u16 = 0x1ff;
pub const PAGE_OFFSET_MAX: u16 = 0xfff;

pub const fn make_virtual_address(l4: u16, l3: u16, l2: u16, l1: u16, offset: u16) -> u64 {
	let mut result = 0
		| (((l4 as u64) & 0x1ff) << L4_SHIFT)
		| (((l3 as u64) & 0x1ff) << L3_SHIFT)
		| (((l2 as u64) & 0x1ff) << L2_SHIFT)
		| (((l1 as u64) & 0x1ff) << L1_SHIFT)
		| ((offset as u64) & 0xfff);
	if (result & (1 << 47)) != 0 {
		result |= 0xffff << 48;
	}
	result
}

const fn generate_offset_table() -> PageTable {
	let mut pt = PageTable {
		entries: [ConstDefault::const_default(); 512],
	};

	let mut i = 0;
	while i < pt.entries.len() {
		pt.entries[i] = arch::Entry::new_from_address(
			(i as u64) * VERY_LARGE_PAGE_SIZE,
			arch::EntryType::VeryLarge,
		)
		.as_writable(true)
		.as_cacheable(false);
		i += 1;
	}

	pt
}

// this is static because it may technically be written to, but we never write to it on purpose. if it gets written to, that's a mistake.
static mut OFFSET_PAGE_TABLE: PageTable = generate_offset_table();

pub(crate) fn initialize(
	memory_regions: &[MemoryRegion],
	kernel_image_info: &KernelImageInfo,
) -> Result<(), ()> {
	// set up the physical memory offset mapping
	{
		// SAFETY: this is safe because there is no other code running at the moment; we're the only ones that can possibly access this page table.
		let curr_root = unsafe { &mut *arch::root_page_table_pointer_phys() };

		// SAFETY: this is safe because we never modify or even access this table ever again.
		let table_virt = (unsafe { &mut OFFSET_PAGE_TABLE } as *mut PageTable) as u64;

		// FIXME: here, we assume that the kernel is mapped directly to `KERNEL_VIRTUAL_START`
		let table_phys =
			(table_virt - KERNEL_VIRTUAL_START) + (kernel_image_info.physical_base_address as u64);

		curr_root.entries[PHYSICAL_MEMORY_L4_INDEX] =
			arch::Entry::new_from_address(table_phys, arch::EntryType::Table).as_writable(true);
	}

	pmm::initialize(memory_regions).expect("The PMM should initialize without error");
	kprintln!("PMM initialized");

	Ok(())
}

/// Retrieves a virtual pointer to the current root (L4) page table.
///
/// # Safety
///
/// This operation is unsafe because of aliasing; Rust assumes it is the only one accessing the table.
unsafe fn root_page_table_pointer() -> *mut PageTable {
	((root_page_table_pointer_phys() as u64) + PHYSICAL_MAPPED_BASE) as *mut PageTable
}

/// Converts the given virtual address to a physical address using a manual table walk.
///
/// # Safety
///
/// This operation is unsafe because of aliasing; Rust assumes it is the only one accessing the table.
pub(crate) unsafe fn virt_to_phys(virt_addr: u64) -> u64 {
	let decomp: DecomposedAddress = virt_addr.const_into();
	let root = root_page_table_pointer();

	kprintln!(
		"L4 index: {}; L4 entry: {:?}",
		decomp.l4,
		(*root).entries[decomp.l4 as usize]
	);

	let l4 =
		((*root).entries[decomp.l4 as usize].address() + PHYSICAL_MAPPED_BASE) as *mut PageTable;
	let l3_entry = (*l4).entries[decomp.l3 as usize];
	match l3_entry.entry_type(3) {
		EntryType::Table => {},
		EntryType::VeryLarge => {
			return l3_entry.address()
				| make_virtual_address(0, 0, decomp.l2, decomp.l1, decomp.offset);
		},
		_ => panic!("Invalid entry type"),
	}

	let l3 = (l3_entry.address() + PHYSICAL_MAPPED_BASE) as *mut PageTable;
	let l2_entry = (*l3).entries[decomp.l2 as usize];
	match l2_entry.entry_type(2) {
		EntryType::Table => {},
		EntryType::Large => {
			return l2_entry.address() | make_virtual_address(0, 0, 0, decomp.l1, decomp.offset);
		},
		_ => panic!("Invalid entry type"),
	}

	let l2 = (l2_entry.address() + PHYSICAL_MAPPED_BASE) as *mut PageTable;
	let l1 = (*l2).entries[decomp.l1 as usize].address();

	l1 | (decomp.offset as u64)
}
