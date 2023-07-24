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
//! of the rest of memory management.

use core::{
	marker::PhantomData,
	mem::{align_of, size_of},
	ops::Deref,
	ptr::{addr_of_mut, drop_in_place, NonNull},
	sync::atomic::{fence, AtomicUsize, Ordering},
};

use super::{pmm::PhysicalFrame, pslab::PSlabRef, util::round_up_page_div};

enum ArcFrameBackingMemory<T> {
	/// Allocate an individual PhysicalFrame just for this ArcFrame.
	Individual(PhysicalFrame),
	// Put the ArcFrame into a slab of memory along with other ArcFrames of the same type.
	Slab(PSlabRef<T>),
}

struct ArcFrameInner<T> {
	counter: AtomicUsize,
	backing_memory: ArcFrameBackingMemory<T>,
	content: T,
}

pub(super) struct ArcFrame<T> {
	allocation: NonNull<ArcFrameInner<T>>,
	phantom: PhantomData<ArcFrameInner<T>>,
}

impl<T> ArcFrame<T> {
	pub(super) fn new(value: T) -> Self {
		assert!(align_of::<T>() <= align_of::<usize>());

		let frame =
			PhysicalFrame::allocate(round_up_page_div(size_of::<ArcFrameInner<T>>() as u64))
				.expect("should be able to allocate physical frame(s) for ArcFrame");
		let inner_ptr = frame.address().as_mut_ptr::<ArcFrameInner<T>>();
		let inner_uninit =
			unsafe { inner_ptr.as_uninit_mut() }.expect("frame pointer must not be null");
		inner_uninit.write(ArcFrameInner {
			counter: AtomicUsize::new(1),
			backing_memory: ArcFrameBackingMemory::Individual(frame),
			content: value,
		});
		// note that we do *not* want to use `assume_init` because we do not want to take ownership of the data.
		// nor do we use `assume_init_{mut,ref}` since we don't need a reference to the initialized data (we use a pointer).
		Self {
			// SAFETY: we know that physical frames cannot have null addresses (our frame allocator guarantees that)
			allocation: unsafe { NonNull::new_unchecked(inner_ptr) },
			phantom: PhantomData,
		}
	}

	unsafe fn from_inner(inner: NonNull<ArcFrameInner<T>>) -> Self {
		Self {
			allocation: inner,
			phantom: PhantomData,
		}
	}

	fn inner(&self) -> &ArcFrameInner<T> {
		// SAFETY: we own a reference on the data, so it's perfectly valid for us to dereference it
		unsafe { self.allocation.as_ref() }
	}
}

impl<T: Default> Default for ArcFrame<T> {
	fn default() -> Self {
		Self::new(Default::default())
	}
}

impl<T> Clone for ArcFrame<T> {
	fn clone(&self) -> Self {
		// we can increment the counter with Relaxed memory ordering; the thing that needs to be
		// unrelaxed (so that all thread operations are visible) is decrementing the counter on Drop.
		self.inner().counter.fetch_add(1, Ordering::Relaxed);

		// SAFETY: we've already incremented the reference count
		unsafe { Self::from_inner(self.allocation) }
	}
}

impl<T> Deref for ArcFrame<T> {
	type Target = T;

	fn deref(&self) -> &Self::Target {
		// SAFETY: we own a reference on the data, so it's perfectly valid for us to dereference it
		&self.inner().content
	}
}

unsafe impl<#[may_dangle] T> Drop for ArcFrame<T> {
	fn drop(&mut self) {
		// for decrementing the refcount, we *do* need to use release ordering so that
		// anything that all of the actions in this thread are visible when the object is
		// fully destroyed.
		if self.inner().counter.fetch_sub(1, Ordering::Release) != 1 {
			return;
		}

		// this fence synchronizes with the release above so that the thread that is destroying
		// the data (this thread) can see everything that happened until all the references were released.
		fence(Ordering::Acquire);

		// SAFETY: we know that no one else can possibly be referencing the inner data at this point
		//         because we're the ones destroying it.
		//
		// read the backing memory handle so we can drop it later
		let backing_memory =
			unsafe { addr_of_mut!((*self.allocation.as_ptr()).backing_memory).read() };

		// SAFETY: we know this data is initialized (we were previously using it, after all), we know
		//         the backing memory is still valid, and we know that we're the only ones dropping it.
		unsafe { drop_in_place(addr_of_mut!((*self.allocation.as_ptr()).content)) };

		// we're now free to drop the backing memory
		// (we could just let it go out of scope, but let's be explicit)
		drop(backing_memory);
	}
}

unsafe impl<T: Send + Sync> Send for ArcFrame<T> {}
unsafe impl<T: Send + Sync> Sync for ArcFrame<T> {}
