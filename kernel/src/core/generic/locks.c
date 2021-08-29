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
 *
 * @file src/core/generic/locks.c
 *
 * Generic lock implementations.
 *
 */

#include <ferro/core/locks.h>
#include <ferro/core/interrupts.h>
#include <ferro/core/threads.private.h>
#include <ferro/core/waitq.private.h>
#include <ferro/core/entry.h>

void flock_spin_init(flock_spin_t* lock) {
	lock->flag = 0;
};

void flock_spin_lock(flock_spin_t* lock) {
	while (__atomic_test_and_set(&lock->flag, __ATOMIC_ACQUIRE)) {
		farch_lock_spin_yield();
	}
};

bool flock_spin_try_lock(flock_spin_t* lock) {
	return !__atomic_test_and_set(&lock->flag, __ATOMIC_ACQUIRE);
};

void flock_spin_unlock(flock_spin_t* lock) {
	__atomic_clear(&lock->flag, __ATOMIC_RELEASE);
};

void flock_spin_intsafe_init(flock_spin_intsafe_t* lock) {
	flock_spin_init(&lock->base);
};

void flock_spin_intsafe_lock(flock_spin_intsafe_t* lock) {
	fint_disable();
	flock_spin_intsafe_lock_unsafe(lock);
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

// the waitq's lock also protects the semaphore state

void flock_semaphore_init(flock_semaphore_t* semaphore, uint64_t initial_count) {
	semaphore->up_count = initial_count;
	fwaitq_init(&semaphore->waitq);
};

static void flock_semaphore_interrupt_wakeup(void* data) {
	bool* keep_looping = data;
	*keep_looping = false;
};

// needs the waitq lock to be held; returns with it dropped
static void flock_semaphore_wait(flock_semaphore_t* semaphore) {
	// TODO: warn if we're going to block while in an interrupt context.
	//       that should definitely not be happening.
	if (fint_is_interrupt_context()) {
		fwaitq_waiter_t waiter;
		bool keep_looping = true;

		fwaitq_waiter_init(&waiter, flock_semaphore_interrupt_wakeup, &keep_looping);

		fwaitq_add_locked(&semaphore->waitq, &waiter);
		fwaitq_unlock(&semaphore->waitq);

		while (keep_looping) {
			fentry_idle();
		}
	} else {
		// `fthread_wait_locked` will drop the waitq lock later
		FERRO_WUR_IGNORE(fthread_wait_locked(fthread_current(), &semaphore->waitq));
	}
};

void flock_semaphore_up(flock_semaphore_t* semaphore) {
	fwaitq_lock(&semaphore->waitq);
	if (semaphore->up_count++ == 0) {
		fwaitq_wake_many_locked(&semaphore->waitq, 1);
	}
	fwaitq_unlock(&semaphore->waitq);
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
