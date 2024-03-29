/*
 * This file is part of Anillo OS
 * Copyright (C) 2021 Anillo OS Developers
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

#include <libsys/locks.private.h>
#include <ferro/platform.h>
#include <gen/libsyscall/syscall-wrappers.h>
#include <libsys/threads.h>

//
// spinlock
//

void sys_spinlock_init(sys_spinlock_t* spinlock) {
	spinlock->internal = 0;
};

void sys_spinlock_lock(sys_spinlock_t* spinlock) {
	while (__atomic_exchange_n(&spinlock->internal, 1, __ATOMIC_ACQUIRE) == 1) {
#if FERRO_ARCH == FERRO_ARCH_x86_64
		__asm__ volatile("pause" :::);
#elif FERRO_ARCH == FERRO_ARCH_aarch64
		__asm__ volatile("yield" :::);
#endif
	}
};

void sys_spinlock_unlock(sys_spinlock_t* spinlock) {
	__atomic_store_n(&spinlock->internal, 0, __ATOMIC_RELEASE);
};

bool sys_spinlock_try_lock(sys_spinlock_t* spinlock) {
	uint8_t expected = 0;
	return __atomic_compare_exchange_n(&spinlock->internal, &expected, 1, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
};

//
// mutex
//
// based on https://github.com/bugaevc/lets-write-sync-primitives
//

void sys_mutex_init(sys_mutex_t* mutex) {
	mutex->internal = 0;
};

void sys_mutex_lock(sys_mutex_t* mutex) {
	uint64_t old_state = sys_mutex_state_unlocked;
	if (__atomic_compare_exchange_n(&mutex->internal, &old_state, sys_mutex_state_locked_uncontended, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
		// great, we got the lock quickly
		// (this is the most common case)
		return;
	}

	// otherwise, we have to take the slow-path and wait

	if (old_state != sys_mutex_state_locked_contended) {
		old_state = __atomic_exchange_n(&mutex->internal, sys_mutex_state_locked_contended, __ATOMIC_ACQUIRE);
	}

	while (old_state != sys_mutex_state_unlocked) {
		libsyscall_wrapper_futex_wait(&mutex->internal, 0, sys_mutex_state_locked_contended, 0, 0, 0);
		old_state = __atomic_exchange_n(&mutex->internal, sys_mutex_state_locked_contended, __ATOMIC_ACQUIRE);
	}
};

void sys_mutex_unlock(sys_mutex_t* mutex) {
	uint64_t old_state = __atomic_exchange_n(&mutex->internal, sys_mutex_state_unlocked, __ATOMIC_RELEASE);

	if (old_state == sys_mutex_state_locked_contended) {
		// if it's contended, we need to wake someone up
		libsyscall_wrapper_futex_wake(&mutex->internal, 0, 1, 0);
	}
};

bool sys_mutex_try_lock(sys_mutex_t* mutex) {
	uint64_t old_state = sys_mutex_state_unlocked;
	return __atomic_compare_exchange_n(&mutex->internal, &old_state, sys_mutex_state_locked_uncontended, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
};

void sys_mutex_lock_sigsafe(sys_mutex_t* mutex) {
	uint64_t old_state = sys_mutex_state_unlocked;

	sys_thread_block_signals(sys_thread_current());

	if (__atomic_compare_exchange_n(&mutex->internal, &old_state, sys_mutex_state_locked_uncontended, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
		// great, we got the lock quickly
		// (this is the most common case)
		return;
	}

	// otherwise, we have to take the slow-path and wait

	if (old_state != sys_mutex_state_locked_contended) {
		old_state = __atomic_exchange_n(&mutex->internal, sys_mutex_state_locked_contended, __ATOMIC_ACQUIRE);
	}

	while (old_state != sys_mutex_state_unlocked) {
		sys_thread_unblock_signals(sys_thread_current());
		libsyscall_wrapper_futex_wait(&mutex->internal, 0, sys_mutex_state_locked_contended, 0, 0, 0);
		sys_thread_block_signals(sys_thread_current());
		old_state = __atomic_exchange_n(&mutex->internal, sys_mutex_state_locked_contended, __ATOMIC_ACQUIRE);
	}
};

void sys_mutex_unlock_sigsafe(sys_mutex_t* mutex) {
	uint64_t old_state = __atomic_exchange_n(&mutex->internal, sys_mutex_state_unlocked, __ATOMIC_RELEASE);

	if (old_state == sys_mutex_state_locked_contended) {
		// if it's contended, we need to wake someone up
		libsyscall_wrapper_futex_wake(&mutex->internal, 0, 1, 0);
	}

	sys_thread_unblock_signals(sys_thread_current());
};

bool sys_mutex_try_lock_sigsafe(sys_mutex_t* mutex) {
	uint64_t old_state = sys_mutex_state_unlocked;
	sys_thread_block_signals(sys_thread_current());
	bool acquired = __atomic_compare_exchange_n(&mutex->internal, &old_state, sys_mutex_state_locked_uncontended, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
	if (!acquired) {
		sys_thread_unblock_signals(sys_thread_current());
	}
	return acquired;
};

//
// semaphore
//
// based on https://github.com/bugaevc/lets-write-sync-primitives
//

void sys_semaphore_init(sys_semaphore_t* semaphore, uint64_t initial_value) {
	semaphore->internal = initial_value;
};

void sys_semaphore_down(sys_semaphore_t* semaphore) {
	uint64_t old_state = __atomic_load_n(&semaphore->internal, __ATOMIC_RELAXED);
	bool have_waited = false;

	while (true) {
		uint64_t count = old_state & ~sys_semaphore_state_up_needs_to_wake_bit;

		if (count > 0) {
			// there might a chance for us to decrement

			uint64_t new_up_needs_to_wake_bit = old_state & sys_semaphore_state_up_needs_to_wake_bit;
			bool going_to_wake = false;

			if (have_waited && new_up_needs_to_wake_bit == 0) {
				// if we previously slept and were woken up (i.e. `have_waited`), we're responsible for waking other waiters up.
				// however, we're only responsible for that if the up-needs-to-wake bit is not currently set.
				// if it *is* set, then sys_semaphore_up() is responsible for waking others.
				// additionally, we only need to wake other waiters up if the semaphore can be further decremented.
				if (count > 1) {
					going_to_wake = true;
				}

				// set the up-needs-to-wake bit so that the waiters we're about to wake up don't try to wake others up.
				//
				// also set it so that future sys_semaphore_up() calls will know that they need to wake others up.
				// we're only going to wake as many waiters as the semaphore can currently handle;
				// future sys_semaphore_up() calls may change that and we can't possibly now that now.
				new_up_needs_to_wake_bit = sys_semaphore_state_up_needs_to_wake_bit;
			}

			// try to set the new state (count - 1, possibly with the needs-to-wake bit set)
			if (!__atomic_compare_exchange_n(&semaphore->internal, &old_state, (count - 1) | new_up_needs_to_wake_bit, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
				// if we failed to exchange the new state, something changed;
				// let's loop back around and check the new state
				continue;
			}

			if (going_to_wake) {
				libsyscall_wrapper_futex_wake(&semaphore->internal, 0, count - 1, 0);
			}

			// we've successfully decremented the semaphore
			return;
		}

		if (old_state == 0) {
			// if the old state was 0, the up-needs-to-wake bit was not set.
			// we need to set it now so that future sys_semaphore_up() calls will wake us.
			if (!__atomic_compare_exchange_n(&semaphore->internal, &old_state, sys_semaphore_state_up_needs_to_wake_bit, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
				// if we failed to exchange, let's loop around and reevaluate the state
				continue;
			}
		}

		libsyscall_wrapper_futex_wait(&semaphore->internal, 0, sys_semaphore_state_up_needs_to_wake_bit, 0, 0, 0);

		have_waited = true;

		// this is most likely the state we'll see upon reevaluation
		old_state = 1;

		// it's a good guess, but it doesn't matter if it's wrong;
		// we'll get the real value when we try to decrement
	}
};

void sys_semaphore_up(sys_semaphore_t* semaphore) {
	uint64_t old_state = __atomic_fetch_add(&semaphore->internal, 1, __ATOMIC_RELEASE);

	if ((old_state & sys_semaphore_state_up_needs_to_wake_bit) == 0) {
		// if we don't need to wake anyone up, perfect!
		return;
	}

	// clear the up-needs-to-wake bit; the waiter we wake up below will wake other waiters
	old_state = __atomic_fetch_and(&semaphore->internal, ~sys_semaphore_state_up_needs_to_wake_bit, __ATOMIC_RELAXED);
	if ((old_state & sys_semaphore_state_up_needs_to_wake_bit) == 0) {
		// someone else has already taken care of this
		return;
	}

	libsyscall_wrapper_futex_wake(&semaphore->internal, 0, 1, 0);
};

bool sys_semaphore_try_down(sys_semaphore_t* semaphore) {
	uint64_t old_state = __atomic_load_n(&semaphore->internal, __ATOMIC_RELAXED);
	uint64_t count = old_state & ~sys_semaphore_state_up_needs_to_wake_bit;

	if (count == 0) {
		return false;
	}

	return __atomic_compare_exchange_n(&semaphore->internal, &old_state, (count - 1) | (old_state & sys_semaphore_state_up_needs_to_wake_bit), false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
};

//
// event
//
// based on https://github.com/bugaevc/lets-write-sync-primitives
//

void sys_event_init(sys_event_t* event) {
	event->internal = 0;
};

void sys_event_wait(sys_event_t* event) {
	uint64_t old_state = sys_event_state_unset_no_wait;

	if (__atomic_compare_exchange_n(&event->internal, &old_state, sys_event_state_unset_wait, false, __ATOMIC_ACQUIRE, __ATOMIC_ACQUIRE)) {
		// if we succeeded in setting it to "unset_wait", update our stored `old_state` to match
		old_state = sys_event_state_unset_wait;
	}

	while (old_state != sys_event_state_set) {
		libsyscall_wrapper_futex_wait(&event->internal, 0, old_state, 0, 0, 0);
		old_state = __atomic_load_n(&event->internal, __ATOMIC_ACQUIRE);
	}
};

void sys_event_notify(sys_event_t* event) {
	if (__atomic_exchange_n(&event->internal, sys_event_state_set, __ATOMIC_RELEASE) == sys_event_state_unset_wait) {
		// if "unset_wait", there are waiters we need to wake up
		libsyscall_wrapper_futex_wake(&event->internal, 0, UINT64_MAX, 0);
	}
};

bool sys_event_try_wait(sys_event_t* event) {
	return __atomic_load_n(&event->internal, __ATOMIC_ACQUIRE) == sys_event_state_set;
};
