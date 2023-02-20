use core::{sync::atomic::{AtomicBool, Ordering}, cell::UnsafeCell, ops::{Deref, DerefMut}};

// heavily inspired by the standard library's Mutex and MutexGuard

/// also requires `!Send` supertrait
#[allow(drop_bounds)]
pub trait LockGuard<'a, T: ?Sized + 'a>: Drop + Deref + DerefMut {}

pub trait Lock<T: ?Sized> {
	type Guard<'a>: LockGuard<'a, T> where Self: 'a, T: 'a;

	fn lock<'a, 'b>(&'a self) -> Self::Guard<'b> where 'a: 'b;
	fn try_lock<'a, 'b>(&'a self) -> Option<Self::Guard<'b>> where 'a: 'b;
}

pub trait SyncLockGuard<'a, T: ?Sized + Sync + 'a>: LockGuard<'a, T> + Sync {}

pub trait SyncLock<T: ?Sized + Send>: Lock<T> + Send + Sync {}

pub struct SpinLock<T: ?Sized> {
	state: AtomicBool,
	data: UnsafeCell<T>,
}

pub struct SpinLockGuard<'a, T: ?Sized + 'a> {
	lock: &'a SpinLock<T>,
}

impl<'a, T: ?Sized> Drop for SpinLockGuard<'a, T> {
	fn drop(&mut self) {
		self.lock.state.swap(false, Ordering::Release);
	}
}

impl<'a, T: ?Sized> Deref for SpinLockGuard<'a, T> {
	type Target = T;

	fn deref(&self) -> &Self::Target {
		unsafe { &*self.lock.data.get() }
	}
}

impl<'a, T: ?Sized> DerefMut for SpinLockGuard<'a, T> {
	fn deref_mut(&mut self) -> &mut Self::Target {
		unsafe { &mut *self.lock.data.get() }
	}
}

impl<'a, T: ?Sized> !Send for SpinLockGuard<'a, T> {}

impl<'a, T: ?Sized> LockGuard<'a, T> for SpinLockGuard<'a, T> {}

unsafe impl<'a, T: ?Sized + Sync> Sync for SpinLockGuard<'a, T> {}

impl<'a, T: ?Sized + Sync> SyncLockGuard<'a, T> for SpinLockGuard<'a, T> {}

impl<T> SpinLock<T> {
	pub fn new(data: T) -> Self {
		Self {
			data: UnsafeCell::new(data),
			state: AtomicBool::new(false),
		}
	}
}

impl<T: ?Sized> Lock<T> for SpinLock<T> {
	type Guard<'a> = SpinLockGuard<'a, T> where Self: 'a, T: 'a;

	fn lock<'a, 'b>(&'a self) -> Self::Guard<'b> where 'a: 'b {
		loop {
			if !self.state.swap(true, Ordering::Acquire) {
				// previous value was false -> not locked
				break;
			}
		}
		SpinLockGuard { lock: self }
	}

	fn try_lock<'a, 'b>(&'a self) -> Option<Self::Guard<'b>> where 'a: 'b {
		if !self.state.swap(true, Ordering::Acquire) {
			// wasn't locked
			Some(SpinLockGuard { lock: self })
		} else {
			None
		}
	}
}

unsafe impl<T: ?Sized + Send> Send for SpinLock<T> {}
unsafe impl<T: ?Sized + Send> Sync for SpinLock<T> {}
impl<T: ?Sized + Send> SyncLock<T> for SpinLock<T> {}

impl<T: ?Sized + Default> Default for SpinLock<T> {
	fn default() -> Self {
		Self::new(Default::default())
	}
}
