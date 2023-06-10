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

pub use super::common::InterruptState;

pub const INTERRUPT_BIT: u64 = 1 << 9;

/// Disables interrupts unconditionally.
pub fn disable_interrupts() {
	// SAFETY: disabling interrupts should be perfectly safe as there's no possibility to violate any compiler assumptions.
	unsafe {
		asm!("cli", options(nostack));
	}
}

/// Enables interrupts unconditionally.
///
/// # Safety
///
/// This operation is unsafe because it allows interrupt code to run, which may violate safety rules such as aliasing if care is not taken.
pub unsafe fn enable_interrupts() {
	asm!("sti", options(nostack));
}

pub fn read_processor_flags() -> u64 {
	let flags: u64;

	// SAFETY: reading the processor flags is perfectly safe; we're not modifying them.
	unsafe {
		asm!(
			"pushfq",
			"pop {}",
			out(reg) flags,
			options(preserves_flags, nomem)
		);
	}

	flags
}

pub fn interrupts_enabled() -> bool {
	(read_processor_flags() & INTERRUPT_BIT) != 0
}

/// Saves the current interrupt state, disables interrupts, and then returns the saved interrupt state.
pub fn disable_and_save_interrupts() -> InterruptState {
	let enabled = interrupts_enabled();
	disable_interrupts();
	InterruptState(enabled)
}

/// Enables interrupts if they were previously enabled.
///
/// # Safety
///
/// This operation is unsafe for the same reason [`enable_interrupts()`] is unsafe.
pub unsafe fn restore_interrupts(interrupt_state: InterruptState) {
	if interrupt_state.0 {
		enable_interrupts();
	}
}
