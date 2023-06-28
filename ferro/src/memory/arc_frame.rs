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

//! This module implements a structure like the standard library's Arc, but
//! with a backing allocation located in a physical frame. This is meant
//! to be used internally within the memory module only for the building blocks
//! of the rest of memory management (currently, this means only AddressSpace).

use core::sync::atomic::AtomicUsize;

struct ArcFrameInner<T> {
	counter: AtomicUsize,
	content: T,
}

pub(super) struct ArcFrame<T> {
	allocation: *mut ArcFrameInner<T>,
}
