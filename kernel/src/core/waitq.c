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
 * waitq initialization and management.
 */

#include <ferro/core/waitq.private.h>

void fwaitq_lock(fwaitq_t* waitq) {
	flock_spin_intsafe_lock(&waitq->lock);
};

void fwaitq_unlock(fwaitq_t* waitq) {
	flock_spin_intsafe_unlock(&waitq->lock);
};

void fwaitq_add_locked(fwaitq_t* waitq, fwaitq_waiter_t* waiter) {
	waiter->prev = waitq->tail;
	waiter->next = NULL;

	if (waiter->prev) {
		waiter->prev->next = waiter;
	}

	if (!waitq->head) {
		waitq->head = waiter;
	}
	waitq->tail = waiter;
};

void fwaitq_remove_locked(fwaitq_t* waitq, fwaitq_waiter_t* waiter) {
	if (waiter == waitq->head) {
		waitq->head = waiter->next;
	}
	if (waiter == waitq->tail) {
		waitq->tail = waiter->prev;
	}

	if (waiter->prev) {
		waiter->prev->next = waiter->next;
	}
	if (waiter->next) {
		waiter->next->prev = waiter->prev;
	}

	waiter->prev = NULL;
	waiter->next = NULL;
};

void fwaitq_waiter_init(fwaitq_waiter_t* waiter, fwaitq_waiter_wakeup_f wakeup, void* data) {
	waiter->prev = NULL;
	waiter->next = NULL;
	waiter->wakeup = wakeup;
	waiter->data = data;
};

void fwaitq_init(fwaitq_t* waitq) {
	waitq->head = NULL;
	waitq->tail = NULL;
	flock_spin_intsafe_init(&waitq->lock);
};

void fwaitq_wait(fwaitq_t* waitq, fwaitq_waiter_t* waiter) {
	fwaitq_lock(waitq);
	fwaitq_add_locked(waitq, waiter);
	fwaitq_unlock(waitq);
};

void fwaitq_wake_many_locked(fwaitq_t* waitq, size_t count) {
	// determine how many waiters we currently have to know our limit for how many to wake up
	//
	// we need to know this because some waiters re-add themselves to the waitq (to wait for future wakeups)
	// and we need to know not to wake them up again in this pass
	//
	// FIXME: this currently doesn't handle the case of a waiter we want to wake up being removed before we can
	//        wake it up in this pass. for example, suppose this is the waitq upon entry:
	//            A, B, C (limit = 3)
	//        then, we wake A up.
	//            B, C (limit = 2)
	//        however, during the time in which the lock is dropped, B removes itself.
	//            C (limit = 2)
	//        we then wake C up.
	//            <nothing> (limit = 1)
	//        however, C decided to re-add itself when we woke it up.
	//            C (limit = 1)
	//        since our limit is still 1, we wake C up again.
	//        granted, waiters are allowed to be woken up spuriously, so this isn't too big of a deal.
	//        still, if we could avoid this somehow, that'd be better.
	size_t limit = 0;

	for (fwaitq_waiter_t* waiter = waitq->head; waiter != NULL; waiter = waiter->next) {
		++limit;
	}

	if (count > limit) {
		count = limit;
	}

	while (waitq->head && count > 0) {
		fwaitq_waiter_t* waiter = waitq->head;

		fwaitq_remove_locked(waitq, waiter);
		fwaitq_unlock(waitq);
		waiter->wakeup(waiter->data);
		fwaitq_lock(waitq);

		--count;
	}
};

void fwaitq_wake_many(fwaitq_t* waitq, size_t count) {
	fwaitq_lock(waitq);
	fwaitq_wake_many_locked(waitq, count);
	fwaitq_unlock(waitq);
};

void fwaitq_unwait(fwaitq_t* waitq, fwaitq_waiter_t* waiter) {
	fwaitq_lock(waitq);
	fwaitq_remove_locked(waitq, waiter);
	fwaitq_unlock(waitq);
};

void fwaitq_wake_specific(fwaitq_t* waitq, fwaitq_waiter_t* waiter) {
	fwaitq_unwait(waitq, waiter);
	waiter->wakeup(waiter->data);
};

bool fwaitq_empty_locked(fwaitq_t* waitq) {
	return !waitq->head;
};
