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

/**
 * @file
 *
 * Generic lock implementations.
 */

#include <ferro/core/locks.h>
#include <ferro/core/interrupts.h>
#include <ferro/core/threads.private.h>
#include <ferro/core/waitq.private.h>
#include <ferro/core/entry.h>
#include <ferro/core/panic.h>
#include <ferro/core/cpu.private.h>

//
// spin locks
//

void flock_spin_init(flock_spin_t* lock) {
	lock->flag = 0;
};

void flock_spin_lock(flock_spin_t* lock) {
	while (__atomic_exchange_n(&lock->flag, 1, __ATOMIC_ACQUIRE) != 0) {
		farch_lock_spin_yield();
	}
};

bool flock_spin_try_lock(flock_spin_t* lock) {
	return __atomic_exchange_n(&lock->flag, 1, __ATOMIC_ACQUIRE) == 0;
};

void flock_spin_unlock(flock_spin_t* lock) {
	if (__atomic_exchange_n(&lock->flag, 0, __ATOMIC_RELEASE) != 1) {
		fpanic("Lock unlocked, but was not previously locked");
	}
};

void flock_spin_intsafe_init(flock_spin_intsafe_t* lock) {
	flock_spin_init(&lock->base);
};

void flock_spin_intsafe_lock(flock_spin_intsafe_t* lock) {
	fint_disable();

	while (__atomic_exchange_n(&lock->base.flag, 1, __ATOMIC_ACQUIRE) != 0) {

		// HACK: because we have interrupts disabled, we need to process this work here.
		//       this is a terrible hack (i'd prefer to simply not do any lock-dependent work that
		//       also needs IPIs), but it's good enough to get by for now.
		//
		//       this is currently necessary due to the paging subsystem.
		//
		//       also, don't freak out about checking `head` without holding its lock;
		//       since we're spinning, we'll just check it again later. that check is mainly there
		//       for early boot where we can't use `FARCH_PER_CPU` yet (but we also don't have any IPI work).
		if (lock != &fcpu_broadcast_queue.lock && fcpu_broadcast_queue.head != NULL) {
			fcpu_do_work();
		}

		farch_lock_spin_yield();
	}
};

void flock_spin_intsafe_lock_unsafe(flock_spin_intsafe_t* lock) {
	return flock_spin_lock(&lock->base);
};

bool flock_spin_intsafe_try_lock(flock_spin_intsafe_t* lock) {
	bool acquired;

	fint_disable();

	acquired = flock_spin_intsafe_try_lock_unsafe(lock);

	if (!acquired) {
		fint_enable();
	}

	return acquired;
};

bool flock_spin_intsafe_try_lock_unsafe(flock_spin_intsafe_t* lock) {
	return flock_spin_try_lock(&lock->base);
};

void flock_spin_intsafe_unlock(flock_spin_intsafe_t* lock) {
	flock_spin_intsafe_unlock_unsafe(lock);
	fint_enable();
};

void flock_spin_intsafe_unlock_unsafe(flock_spin_intsafe_t* lock) {
	return flock_spin_unlock(&lock->base);
};

//
// semaphores
//

// the waitq's lock also protects the semaphore state

void flock_semaphore_init(flock_semaphore_t* semaphore, uint64_t initial_count) {
	semaphore->up_count = initial_count;
	fwaitq_init(&semaphore->waitq);
};

static void flock_semaphore_interrupt_wakeup(void* data) {
	volatile bool* keep_looping = data;
	*keep_looping = false;
};

// needs the waitq lock to be held; returns with it dropped
static void flock_semaphore_wait(flock_semaphore_t* semaphore) {
	// TODO: warn if we're going to block while in an interrupt context.
	//       that should definitely not be happening.

	// the `!fthread_current()` check is in case we're trying to lock a semaphore early in kernel startup, where we don't have threads yet
	if (fint_is_interrupt_context() || !fthread_current()) {
		fwaitq_waiter_t waiter;
		volatile bool keep_looping = true;

		fwaitq_waiter_init(&waiter, flock_semaphore_interrupt_wakeup, (void*)&keep_looping);

		fwaitq_add_locked(&semaphore->waitq, &waiter);
		fwaitq_unlock(&semaphore->waitq);

		while (keep_looping) {
			fentry_idle();
		}
	} else {
		// fthread_wait_locked() will drop the waitq lock later
		FERRO_WUR_IGNORE(fthread_wait_locked(fthread_current(), &semaphore->waitq));
	}
};

bool flock_semaphore_up(flock_semaphore_t* semaphore) {
	bool awoken = false;
	fwaitq_lock(&semaphore->waitq);
	if (semaphore->up_count++ == 0) {
		fwaitq_wake_many_locked(&semaphore->waitq, 1);
		awoken = true;
	}
	fwaitq_unlock(&semaphore->waitq);
	return awoken;
};

void flock_semaphore_down(flock_semaphore_t* semaphore) {
	while (true) {
		fwaitq_lock(&semaphore->waitq);
		if (semaphore->up_count == 0) {
			flock_semaphore_wait(semaphore);
			continue;
		}
		--semaphore->up_count;
		fwaitq_unlock(&semaphore->waitq);
		break;
	}
};

ferr_t flock_semaphore_try_down(flock_semaphore_t* semaphore) {
	ferr_t result = ferr_ok;

	fwaitq_lock(&semaphore->waitq);
	if (semaphore->up_count == 0) {
		result = ferr_temporary_outage;
	} else {
		--semaphore->up_count;
	}
	fwaitq_unlock(&semaphore->waitq);

	return result;
};

ferr_t flock_semaphore_down_interruptible(flock_semaphore_t* semaphore) {
	while (true) {
		if (fthread_current() && fthread_marked_interrupted(fthread_current())) {
			return ferr_signaled;
		}

		fwaitq_lock(&semaphore->waitq);
		if (semaphore->up_count == 0) {
			flock_semaphore_wait(semaphore);
			continue;
		}
		--semaphore->up_count;
		fwaitq_unlock(&semaphore->waitq);
		break;
	}
	return ferr_ok;
};

//
// mutexes
//

void flock_mutex_init(flock_mutex_t* mutex) {
	mutex->owner = UINT64_MAX;
	mutex->lock_count = 0;
	fwaitq_init(&mutex->waitq);
};

static void flock_mutex_interrupt_wakeup(void* data) {
	volatile bool* keep_looping = data;
	*keep_looping = false;
};

// needs the waitq lock to be held; returns with it dropped
static void flock_mutex_wait(flock_mutex_t* mutex) {
	// TODO: warn if we're going to block while in an interrupt context.
	//       that should definitely not be happening.

	if (fint_is_interrupt_context()) {
		fwaitq_waiter_t waiter;
		volatile bool keep_looping = true;

		fwaitq_waiter_init(&waiter, flock_mutex_interrupt_wakeup, (void*)&keep_looping);

		fwaitq_add_locked(&mutex->waitq, &waiter);
		fwaitq_unlock(&mutex->waitq);

		while (keep_looping) {
			fentry_idle();
		}
	} else {
		// fthread_wait_locked() will drop the waitq lock later
		FERRO_WUR_IGNORE(fthread_wait_locked(fthread_current(), &mutex->waitq));
	}
};

void flock_mutex_lock(flock_mutex_t* mutex) {
	if (!fthread_current()) {
		fpanic("Mutexes can only be used once the kernel has entered threading mode");
	}

	while (true) {
		fwaitq_lock(&mutex->waitq);

		if (mutex->lock_count > 0 && mutex->owner != fthread_current()->id) {
			flock_mutex_wait(mutex);
			continue;
		}

		mutex->owner = fthread_current()->id;
		++mutex->lock_count;

		fwaitq_unlock(&mutex->waitq);
		break;
	}
};

FERRO_WUR ferr_t flock_mutex_try_lock(flock_mutex_t* mutex) {
	ferr_t result = ferr_ok;

	if (!fthread_current()) {
		fpanic("Mutexes can only be used once the kernel has entered threading mode");
	}

	fwaitq_lock(&mutex->waitq);

	if (mutex->lock_count > 0 && mutex->owner != fthread_current()->id) {
		result = ferr_temporary_outage;
	} else {
		mutex->owner = fthread_current()->id;
		++mutex->lock_count;
	}

	fwaitq_unlock(&mutex->waitq);

	return result;
};

ferr_t flock_mutex_lock_interruptible(flock_mutex_t* mutex) {
	if (!fthread_current()) {
		fpanic("Mutexes can only be used once the kernel has entered threading mode");
	}

	while (true) {
		if (fthread_marked_interrupted(fthread_current())) {
			return ferr_signaled;
		}

		fwaitq_lock(&mutex->waitq);

		if (mutex->lock_count > 0 && mutex->owner != fthread_current()->id) {
			flock_mutex_wait(mutex);
			continue;
		}

		mutex->owner = fthread_current()->id;
		++mutex->lock_count;

		fwaitq_unlock(&mutex->waitq);
		break;
	}

	return ferr_ok;
};

void flock_mutex_unlock(flock_mutex_t* mutex) {
	if (!fthread_current()) {
		fpanic("Mutexes can only be used once the kernel has entered threading mode");
	}

	fwaitq_lock(&mutex->waitq);

	if (mutex->owner != fthread_current()->id) {
		fpanic("Mutex unlocked by non-owning thread");
	} else if (mutex->lock_count == 0) {
		fpanic("Mutex over-unlocked");
	}

	if (--mutex->lock_count == 0) {
		mutex->owner = UINT64_MAX;
	}

	fwaitq_wake_many_locked(&mutex->waitq, 1);

	fwaitq_unlock(&mutex->waitq);
};

// TODO: when a thread is holding a mutex, it should know this so that the mutex can be unlocked if the thread dies.

//
// rw locks
//

// FIXME: this RW lock can certainly be optimized (a lot)

// TODO: test this RW lock implementation!

FERRO_ENUM(uint64_t, flock_rw_state_bits) {
	flock_rw_state_bit_locked_write    = 1ull << 63,
	flock_rw_state_bit_writers_waiting = 1ull << 62,
	flock_rw_state_mask_read_count     = ~(0ull) >> 2,
};

void flock_rw_init(flock_rw_t* rw) {
	rw->state = 0;
	fwaitq_init(&rw->read_waitq);
	fwaitq_init(&rw->write_waitq);
};

static void flock_rw_interrupt_wakeup(void* data) {
	volatile bool* keep_looping = data;
	*keep_looping = false;
};

// needs both waitq locks to be held; returns with them dropped
static void flock_rw_wait(flock_rw_t* rw, bool writing) {
	// TODO: warn if we're going to block while in an interrupt context.
	//       that should definitely not be happening.
	fwaitq_t* waitq = writing ? &rw->write_waitq : &rw->read_waitq;
	fwaitq_t* other_waitq = writing ? &rw->read_waitq : &rw->write_waitq;

	fwaitq_unlock(other_waitq);

	if (fint_is_interrupt_context()) {
		fwaitq_waiter_t waiter;
		volatile bool keep_looping = true;

		fwaitq_waiter_init(&waiter, flock_rw_interrupt_wakeup, (void*)&keep_looping);

		fwaitq_add_locked(waitq, &waiter);
		fwaitq_unlock(waitq);

		while (keep_looping) {
			fentry_idle();
		}
	} else {
		FERRO_WUR_IGNORE(fthread_wait_locked(fthread_current(), waitq));
	}
};

void flock_rw_lock_read(flock_rw_t* rw) {
	while (true) {
		fwaitq_lock(&rw->read_waitq);
		fwaitq_lock(&rw->write_waitq);

		if ((rw->state & flock_rw_state_bit_locked_write) == 0) {
			// slow path; we have to wait for the writer to finish
			flock_rw_wait(rw, false);
			continue;
		}

		rw->state = (rw->state & ~flock_rw_state_mask_read_count) | ((rw->state + 1) & flock_rw_state_mask_read_count);

		fwaitq_unlock(&rw->write_waitq);
		fwaitq_unlock(&rw->read_waitq);
		break;
	}
};

ferr_t flock_rw_try_lock_read(flock_rw_t* rw) {
	ferr_t result = ferr_ok;

	fwaitq_lock(&rw->read_waitq);
	fwaitq_lock(&rw->write_waitq);

	if ((rw->state & flock_rw_state_bit_locked_write) == 0) {
		// we would have to wait for the writer to finish
		result = ferr_temporary_outage;
	} else {
		rw->state = (rw->state & ~flock_rw_state_mask_read_count) | ((rw->state + 1) & flock_rw_state_mask_read_count);
	}

	fwaitq_unlock(&rw->write_waitq);
	fwaitq_unlock(&rw->read_waitq);

	return result;
};

ferr_t flock_rw_lock_read_interruptible(flock_rw_t* rw) {
	while (true) {
		if (fthread_marked_interrupted(fthread_current())) {
			return ferr_signaled;
		}

		fwaitq_lock(&rw->read_waitq);
		fwaitq_lock(&rw->write_waitq);

		if ((rw->state & flock_rw_state_bit_locked_write) == 0) {
			// slow path; we have to wait for the writer to finish
			flock_rw_wait(rw, false);
			continue;
		}

		rw->state = (rw->state & ~flock_rw_state_mask_read_count) | ((rw->state + 1) & flock_rw_state_mask_read_count);

		fwaitq_unlock(&rw->write_waitq);
		fwaitq_unlock(&rw->read_waitq);
		break;
	}

	return ferr_ok;
};

void flock_rw_lock_write(flock_rw_t* rw) {
	bool waited = false;

	while (true) {
		fwaitq_lock(&rw->read_waitq);
		fwaitq_lock(&rw->write_waitq);

		// for us to write, the state must be 0 or `flock_rw_state_bit_writers_waiting` (if we've already waited), because:
		//   * flock_rw_state_bit_locked_write indicates someone is currently writing, so we would have to wait.
		//   * flock_rw_state_bit_writers_waiting indicates someone is waiting to write, so we would have to wait (unless we were just woken up after waiting).
		//   * flock_rw_state_mask_read_count being greater than 0 indicates at least one person is currently reading, so we would have to wait.
		if (!(
			rw->state == 0 ||
			(waited && rw->state == flock_rw_state_bit_writers_waiting)
		)) {
			// slow path; we have to wait for the writer or readers to finish
			rw->state |= flock_rw_state_bit_writers_waiting;
			flock_rw_wait(rw, true);
			waited = true;
			continue;
		}

		rw->state |= flock_rw_state_bit_locked_write;

		if (fwaitq_empty_locked(&rw->write_waitq)) {
			rw->state &= ~flock_rw_state_bit_writers_waiting;
		}

		fwaitq_unlock(&rw->write_waitq);
		fwaitq_unlock(&rw->read_waitq);
		break;
	}
};

ferr_t flock_rw_try_lock_write(flock_rw_t* rw) {
	ferr_t result = ferr_ok;

	fwaitq_lock(&rw->read_waitq);
	fwaitq_lock(&rw->write_waitq);

	if (rw->state != 0) {
		// we would have to wait for the writer or readers to finish
		result = ferr_temporary_outage;
	} else {
		rw->state |= flock_rw_state_bit_locked_write;

		if (fwaitq_empty_locked(&rw->write_waitq)) {
			rw->state &= ~flock_rw_state_bit_writers_waiting;
		}
	}

	fwaitq_unlock(&rw->write_waitq);
	fwaitq_unlock(&rw->read_waitq);

	return result;
};

ferr_t flock_rw_lock_write_interruptible(flock_rw_t* rw) {
	bool waited = false;

	while (true) {
		if (fthread_marked_interrupted(fthread_current())) {
			return ferr_signaled;
		}

		fwaitq_lock(&rw->read_waitq);
		fwaitq_lock(&rw->write_waitq);

		// for us to write, the state must be 0 or `flock_rw_state_bit_writers_waiting` (if we've already waited), because:
		//   * flock_rw_state_bit_locked_write indicates someone is currently writing, so we would have to wait.
		//   * flock_rw_state_bit_writers_waiting indicates someone is waiting to write, so we would have to wait (unless we were just woken up after waiting).
		//   * flock_rw_state_mask_read_count being greater than 0 indicates at least one person is currently reading, so we would have to wait.
		if (!(
			rw->state == 0 ||
			(waited && rw->state == flock_rw_state_bit_writers_waiting)
		)) {
			// slow path; we have to wait for the writer or readers to finish
			rw->state |= flock_rw_state_bit_writers_waiting;
			flock_rw_wait(rw, true);
			waited = true;
			continue;
		}

		rw->state |= flock_rw_state_bit_locked_write;

		if (fwaitq_empty_locked(&rw->write_waitq)) {
			rw->state &= ~flock_rw_state_bit_writers_waiting;
		}

		fwaitq_unlock(&rw->write_waitq);
		fwaitq_unlock(&rw->read_waitq);
		break;
	}

	return ferr_ok;
};

void flock_rw_unlock(flock_rw_t* rw) {
	fwaitq_lock(&rw->read_waitq);
	fwaitq_lock(&rw->write_waitq);

	if ((rw->state & flock_rw_state_bit_locked_write) != 0) {
		rw->state &= ~flock_rw_state_bit_locked_write;
	} else {
		rw->state = (rw->state & ~flock_rw_state_mask_read_count) | ((rw->state - 1) & flock_rw_state_mask_read_count);
	}

	if ((rw->state & flock_rw_state_mask_read_count) == 0 && (rw->state & flock_rw_state_bit_writers_waiting) != 0) {
		fwaitq_wake_many_locked(&rw->write_waitq, 1);
	} else {
		fwaitq_wake_many_locked(&rw->read_waitq, SIZE_MAX);
	}

	fwaitq_unlock(&rw->write_waitq);
	fwaitq_unlock(&rw->read_waitq);
};
