#![no_std]
#![no_main]

use core::panic::PanicInfo;

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
	 * A pointer to the ACPI XSDT pointer (::facpi_rsdp).
	 */
	RSDPPointer,
}

#[repr(C)]
struct BootDataInfo {

}

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
	loop {}
}

#[no_mangle]
pub extern "C" fn ferro_entry(initial_pool: *mut u8, initial_pool_page_count: usize, raw_boot_data: *const BootDataInfo, boot_data_count: usize) -> ! {
	loop {}
}
