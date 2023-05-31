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

#![no_std]
#![no_main]
#![feature(
	negative_impls,
	format_args_nl,
	const_for,
	const_trait_impl,
	const_slice_index,
	const_mut_refs,
	const_option,
	const_intoiterator_identity,
	maybe_uninit_uninit_array,
	const_refs_to_cell,
	generic_const_exprs,
	generic_arg_infer,
	const_slice_from_raw_parts_mut,
	const_fn_floating_point_arithmetic
)]
#![allow(dead_code, incomplete_features)]

mod framebuffer;
mod sync;

#[macro_use]
mod console;
mod geometry;
mod interrupts;
mod memory;
mod util;

use core::ffi::c_void;
use core::mem::size_of;
use core::panic::PanicInfo;

#[repr(i32)]
#[derive(PartialEq, Clone, Copy, Debug)]
enum BootDataType {
	/// Default value; not a valid value
	None,

	/// A pointer to where our ramdisk is stored.
	Ramdisk,

	/// A pointer to where our config data (a.k.a. boot params) is stored.
	Config,

	/// A pointer to where our kernel image information is stored.
	KernelImageInfo,

	/// A pointer to where our kernel segment information table is stored.
	KernelSegmentInfoTable,

	/// A pointer to where our framebuffer information is stored.
	FramebufferInfo,

	/// A pointer to where a pool of essential/permanent data is stored early in the boot process.
	InitialPool,

	/// A pointer to where our memory map is stored.
	MemoryMap,

	/// A pointer to the ACPI XSDT pointer.
	RSDPPointer,

	/// A pointer to the memory region where the initial kernel page tables are stored.
	PageTables,
}

#[repr(C)]
#[derive(Debug)]
pub struct BootDataInfo {
	ty: BootDataType,
	phys_addr: *mut c_void,
	virt_addr: *mut c_void,
	size: usize,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub(crate) struct KernelImageInfo {
	physical_base_address: *const c_void,
	size: usize,
	segment_count: usize,
	segments: *const KernelSegment,
}

struct KernelSegment {
	size: usize,
	physical_address: *const c_void,
	virtual_address: *const c_void,
}

#[repr(i32)]
#[derive(Debug, PartialEq, Eq, Clone, Copy)]
enum MemoryRegionType {
	///
	/// Default value; not a valid value.
	///
	None,

	///
	/// General multi-purpose memory.
	///
	General,

	///
	/// General multi-purpose memory that also happens to be non-volatile.
	///
	NVRAM,

	///
	/// Memory that is reserved for hardware use.
	///
	/// Not to be arbitrarily touched by the OS, but some devices (e.g. framebuffers, interrupt controllers, etc.) might use this kind of memory for MMIO, in which case
	/// the OS may access/modify the memory **according to how the device dictates it must be used**.
	///
	HardwareReserved,

	///
	/// Memory that is reserved until ACPI is enabled.
	///
	/// Afterwards, it becomes general memory.
	///
	ACPIReclaim,

	///
	/// Memory reserved for processor code.
	///
	/// Never to be touched by the OS.
	///
	PALCode,

	///
	/// Memory where special kernel data is stored on boot.
	///
	/// This is usually permanent.
	///
	KernelReserved,

	///
	/// Memory where the kernel's entry stack is stored.
	///
	/// This is reserved in early boot but can be turned into general memory later.
	///
	KernelStack,
}

#[repr(C)]
pub(crate) struct MemoryRegion {
	ty: MemoryRegionType,
	physical_start: usize,
	virtual_start: usize,
	page_count: usize,
}

#[panic_handler]
fn panic(info: &PanicInfo) -> ! {
	if let Some(loc) = info.location() {
		kprint!("panic at {}:{}", loc.file(), loc.line());
	} else {
		kprint!("panic");
	}
	loop {}
}

#[no_mangle]
pub extern "C" fn ferro_entry(
	_initial_pool: *mut u8,
	_initial_pool_page_count: usize,
	raw_boot_data: *const BootDataInfo,
	boot_data_count: usize,
) -> ! {
	let boot_data = unsafe { core::slice::from_raw_parts(raw_boot_data, boot_data_count) };

	for entry in boot_data {
		match entry.ty {
			BootDataType::FramebufferInfo => {
				let fb_info = unsafe { &*(entry.virt_addr as *const framebuffer::Info) };
				unsafe {
					framebuffer::init(fb_info);
				}
			},
			_ => {},
		}
	}

	kprintln!("Ferro starting up");

	// now that we have a console, go ahead and initialize the PMM and VMM

	let mut memory_map: Option<&[MemoryRegion]> = None;
	let mut kernel_image_info: Option<&KernelImageInfo> = None;

	for entry in boot_data {
		match entry.ty {
			BootDataType::MemoryMap => {
				kprintln!(
					"Memory map found at {:?} ({:?})",
					entry.virt_addr,
					entry.phys_addr
				);
				memory_map = Some(unsafe {
					core::slice::from_raw_parts(
						entry.virt_addr as *const MemoryRegion,
						entry.size / size_of::<MemoryRegion>(),
					)
				})
			},
			BootDataType::KernelImageInfo => {
				kprintln!(
					"Kernel image info found at {:?} ({:?})",
					entry.virt_addr,
					entry.phys_addr
				);
				kernel_image_info = Some(unsafe { &*(entry.virt_addr as *const KernelImageInfo) });
			},
			_ => {},
		}
	}

	let memory_map = memory_map.expect("A memory map is required, but none was found");
	let kernel_image_info =
		kernel_image_info.expect("A kernel image info structure is required, but none was found");

	memory::initialize(memory_map, kernel_image_info)
		.expect("The memory subsystem should initialize without error");
	kprintln!("Memory subsystem initialized");

	loop {}
}
