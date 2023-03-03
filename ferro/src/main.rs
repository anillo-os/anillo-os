#![no_std]
#![no_main]
#![feature(negative_impls, format_args_nl, const_for, const_trait_impl, const_slice_index, const_mut_refs, const_option, const_intoiterator_identity, maybe_uninit_uninit_array, const_refs_to_cell, generic_const_exprs, const_convert)]
#![allow(dead_code, incomplete_features)]

mod framebuffer;
mod sync;

#[macro_use]
mod console;
mod util;
mod geometry;

use core::panic::PanicInfo;
use core::ffi::c_void;

#[repr(i32)]
#[derive(PartialEq, Clone, Copy)]
enum BootDataType {
	/**
	 * Default value; not a valid value
	 */
	None,

	/**
	 * A pointer to where our ramdisk is stored.
	 */
	Ramdisk,

	/**
	 * A pointer to where our config data (a.k.a. boot params) is stored.
	 */
	Config,

	/**
	 * A pointer to where our kernel image information is stored.
	 */
	KernelImageInfo,

	/**
	 * A pointer to where our kernel segment information table is stored.
	 */
	KernelSegmentInfoTable,

	/**
	 * A pointer to where our framebuffer information is stored.
	 */
	FramebufferInfo,

	/**
	 * A pointer to where a pool of essential/permanent data is stored early in the boot process.
	 */
	InitialPool,

	/**
	 * A pointer to where our memory map is stored.
	 */
	MemoryMap,

	/**
	 * A pointer to the ACPI XSDT pointer.
	 */
	RSDPPointer,
}

#[repr(C)]
pub struct BootDataInfo {
	ty: BootDataType,
	phys_addr: *mut c_void,
	virt_addr: *mut c_void,
	size: usize,
}

#[repr(C)]
#[derive(Clone, Copy)]
struct KernelImageInfo {
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

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
	loop {}
}

#[no_mangle]
pub extern "C" fn ferro_entry(_initial_pool: *mut u8, _initial_pool_page_count: usize, raw_boot_data: *const BootDataInfo, boot_data_count: usize) -> ! {
	let boot_data = unsafe { core::slice::from_raw_parts(raw_boot_data, boot_data_count) };

	for entry in boot_data {
		match entry.ty {
			BootDataType::FramebufferInfo => {
				let fb_info = unsafe { &*(entry.virt_addr as *const framebuffer::Info) };
				unsafe {
					framebuffer::init(fb_info);
				}
			}
			_ => {}
		}
	}

	kprintln!("Test!");

	loop {}
}
