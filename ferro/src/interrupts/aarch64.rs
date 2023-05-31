pub use super::common::InterruptState;

/// Disables interrupts unconditionally.
pub fn disable_interrupts() {
	unimplemented!()
}

/// Enables interrupts unconditionally.
///
/// # Safety
///
/// This operation is unsafe because it allows interrupt code to run, which may violate safety rules such as aliasing if care is not taken.
pub unsafe fn enable_interrupts() {
	unimplemented!()
}

pub fn read_processor_flags() -> u64 {
	unimplemented!()
}

pub fn interrupts_enabled() -> bool {
	unimplemented!()
}

/// Saves the current interrupt state, disables interrupts, and then returns the saved interrupt state.
pub fn disable_and_save_interrupts() -> InterruptState {
	unimplemented!()
}

/// Enables interrupts if they were previously enabled.
///
/// # Safety
///
/// This operation is unsafe for the same reason [`enable_interrupts()`] is unsafe.
pub unsafe fn restore_interrupts(_interrupt_state: InterruptState) {
	unimplemented!()
}
