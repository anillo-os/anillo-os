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

use core::{
	cell::UnsafeCell,
	hint::spin_loop,
	ops::{Deref, DerefMut},
	sync::atomic::{AtomicBool, Ordering},
};

use crate::interrupts::{self, restore_interrupts, InterruptState};

// heavily inspired by the standard library's Mutex and MutexGuard

/// also requires `!Send` supertrait
#[allow(drop_bounds)]
pub trait LockGuard<'a, T: ?Sized + 'a>: Drop + Deref + DerefMut {}

pub trait Lock<T: ?Sized> {
	type Guard<'a>: LockGuard<'a, T>
	where
		Self: 'a,
		T: 'a;

	fn lock<'a, 'b>(&'a self) -> Self::Guard<'b>
	where
		'a: 'b;
	fn try_lock<'a, 'b>(&'a self) -> Option<Self::Guard<'b>>
	where
		'a: 'b;
}

pub trait SyncLockGuard<'a, T: ?Sized + Sync + 'a>: LockGuard<'a, T> + Sync {}

pub trait SyncLock<T: ?Sized + Send>: Lock<T> + Send + Sync {}

/// An interrupt-safe spin lock type.
pub struct SpinLock<T: ?Sized> {
	state: AtomicBool,
	data: UnsafeCell<T>,
}

pub struct SpinLockGuard<'a, T: ?Sized + 'a> {
	lock: &'a SpinLock<T>,
	interrupt_state: InterruptState,
}

impl<'a, T: ?Sized> Drop for SpinLockGuard<'a, T> {
	fn drop(&mut self) {
		self.lock.state.swap(false, Ordering::Release);

		// SAFETY: this is safe with regard to the spin lock itself; we do not do anything that would violate any invariants.
		unsafe { restore_interrupts(self.interrupt_state) };
	}
}

impl<'a, T: ?Sized> Deref for SpinLockGuard<'a, T> {
	type Target = T;

	fn deref(&self) -> &Self::Target {
		// SAFETY: accessing the data is safe because we own the lock, so we're the only ones with access to the data.
		unsafe { &*self.lock.data.get() }
	}
}

impl<'a, T: ?Sized> DerefMut for SpinLockGuard<'a, T> {
	fn deref_mut(&mut self) -> &mut Self::Target {
		// SAFETY: same as the Deref::deref implementation
		unsafe { &mut *self.lock.data.get() }
	}
}

impl<'a, T: ?Sized> !Send for SpinLockGuard<'a, T> {}

impl<'a, T: ?Sized> LockGuard<'a, T> for SpinLockGuard<'a, T> {}

// SAFETY: we guarantee this is safe because spin lock guards only exist while the lock is held, so they can be shared between threads.
unsafe impl<'a, T: ?Sized + Sync> Sync for SpinLockGuard<'a, T> {}

impl<'a, T: ?Sized + Sync> SyncLockGuard<'a, T> for SpinLockGuard<'a, T> {}

impl<T> SpinLock<T> {
	pub const fn new(data: T) -> Self {
		Self {
			data: UnsafeCell::new(data),
			state: AtomicBool::new(false),
		}
	}
}

impl<T: ?Sized> Lock<T> for SpinLock<T> {
	type Guard<'a> = SpinLockGuard<'a, T> where Self: 'a, T: 'a;

	fn lock<'a, 'b>(&'a self) -> Self::Guard<'b>
	where
		'a: 'b,
	{
		let saved_interrupt_state;
		loop {
			let interrupt_state = interrupts::disable_and_save_interrupts();

			if !self.state.swap(true, Ordering::Acquire) {
				// previous value was false -> not locked
				saved_interrupt_state = interrupt_state;
				break;
			}

			// SAFETY: this is fine because we're simply restoring the previous state, and we also didn't
			//         take any new references or anything like that so we're not violating any invariants.
			unsafe { restore_interrupts(interrupt_state) };
			spin_loop();
		}
		SpinLockGuard {
			lock: self,
			interrupt_state: saved_interrupt_state,
		}
	}

	fn try_lock<'a, 'b>(&'a self) -> Option<Self::Guard<'b>>
	where
		'a: 'b,
	{
		let interrupt_state = interrupts::disable_and_save_interrupts();
		if !self.state.swap(true, Ordering::Acquire) {
			// wasn't locked
			Some(SpinLockGuard {
				lock: self,
				interrupt_state,
			})
		} else {
			None
		}
	}
}

// SAFETY: spin locks are locks, so sharing them between threads is safe.
unsafe impl<T: ?Sized + Send> Send for SpinLock<T> {}
unsafe impl<T: ?Sized + Send> Sync for SpinLock<T> {}
impl<T: ?Sized + Send> SyncLock<T> for SpinLock<T> {}

impl<T: Default> Default for SpinLock<T> {
	fn default() -> Self {
		Self::new(Default::default())
	}
}
