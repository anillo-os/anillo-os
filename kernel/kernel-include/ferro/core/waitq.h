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
 * waitq subsystem.
 */

#ifndef _FERRO_CORE_WAITQ_H_
#define _FERRO_CORE_WAITQ_H_

#include <stddef.h>

#include <ferro/base.h>
#include <ferro/core/locks.spin.h>

FERRO_DECLARATIONS_BEGIN;

/**
 * @addtogroup Waitq
 *
 * The waitq subsystem.
 *
 * @{
 */

FERRO_STRUCT_FWD(fthread);

/**
 * A callback that is invoked when the waitq is going to wakeup the waiter associated with it.
 *
 * When invoked, the waitq is not locked, so calling waitq functions is valid.
 */
typedef void (*fwaitq_waiter_wakeup_f)(void* data);

FERRO_STRUCT(fwaitq_waiter) {
	fwaitq_waiter_t* prev;
	fwaitq_waiter_t* next;
	fwaitq_waiter_wakeup_f wakeup;
	void* data;
};

/**
 * waitqs are a sort of FIFO queue. They're designed with a generic wakeup interface to be flexible and multi-purpose.
 * However, they also have special ties to other subsystems like e.g. the threading subsystem, which allows threads to wait for a waitq.
 */
FERRO_STRUCT(fwaitq) {
	fwaitq_waiter_t* head;
	fwaitq_waiter_t* tail;
	flock_spin_intsafe_t lock;
};

#define FWAITQ_INIT {0}

void fwaitq_waiter_init(fwaitq_waiter_t* waiter, fwaitq_waiter_wakeup_f wakeup, void* data);

void fwaitq_init(fwaitq_t* waitq);

/**
 * Adds the given waiter onto the waitq's waiting list.
 *
 * @note This is the WRONG function to use for putting a thread to sleep to wait for a waitq.
 *       For that, use fthread_wait().
 *
 * @note Expanding on the previous note, in general, it is a race condition if you need to perform some operation where you could miss the wakeup call after adding yourself to the waitq's waiting list.
 *       e.g. If you add yourself, someone else wakes you up via the waitq, but then you perform some operation that doesn't check whether your wakeup callback has already been called.
 */
void fwaitq_wait(fwaitq_t* waitq, fwaitq_waiter_t* waiter);

/**
 * Wakes the given number of waiters.
 */
void fwaitq_wake_many(fwaitq_t* waitq, size_t count);

/**
 * Wakes the given waiter.
 */
void fwaitq_wake_specific(fwaitq_t* waitq, fwaitq_waiter_t* waiter);

/**
 * Removes the given waiter from the waitq's waiting list.
 *
 * @note Unlike fwaitq_wake_specific(), this function does NOT notify the waiter.
 *       It simply removes the waiter from the waitq's waiting list.
 */
void fwaitq_unwait(fwaitq_t* waitq, fwaitq_waiter_t* waiter);

/**
 * @}
 */

FERRO_DECLARATIONS_END;

#endif // _FERRO_CORE_WAITQ_H_

