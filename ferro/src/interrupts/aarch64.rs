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
