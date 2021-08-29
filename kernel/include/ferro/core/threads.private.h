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

#ifndef _FERRO_CORE_THREADS_PRIVATE_H_
#define _FERRO_CORE_THREADS_PRIVATE_H_

#include <stdint.h>
#include <stddef.h>

#include <ferro/base.h>
#include <ferro/core/threads.h>

FERRO_DECLARATIONS_BEGIN;

/**
 * Requests that the given thread be suspended as soon as possible.
 *
 * @note Called with the thread lock held.
 */
typedef void (*fthread_manager_suspend_f)(fthread_t* thread);

/**
 * Requests that the given thread be resumed as soon as possible.
 *
 * @note Called with the thread lock held.
 */
typedef void (*fthread_manager_resume_f)(fthread_t* thread);

/**
 * Requests that the given thread be killed as soon as possible.
 *
 * @note Called with the thread lock held.
 */
typedef void (*fthread_manager_kill_f)(fthread_t* thread);

/**
 * Informs the thread manager that the given thread is entering an interrupt.
 *
 * @note Called with the thread lock NOT held.
 */
typedef void (*fthread_manager_interrupted_f)(fthread_t* thread);

/**
 * Informs the thread manager that the given thread is returning from an interrupt.
 *
 * @note Called with the thread lock NOT held.
 */
typedef void (*fthread_manager_ending_interrupt_f)(fthread_t* thread);

FERRO_STRUCT(fthread_manager) {
	fthread_manager_suspend_f suspend;
	fthread_manager_resume_f resume;
	fthread_manager_kill_f kill;
	fthread_manager_interrupted_f interrupted;
	fthread_manager_ending_interrupt_f ending_interrupt;
};

FERRO_STRUCT(fthread_private) {
	fthread_t thread;
	fthread_manager_t* manager;
	void* manager_private;

	uint64_t pending_timeout_value;
	fthread_timeout_type_t pending_timeout_type;
	ftimers_id_t timer_id;
};

FERRO_ALWAYS_INLINE fthread_state_execution_t fthread_state_execution_read_locked(const fthread_t* thread) {
	return (thread->state & fthread_state_execution_mask);
};

FERRO_ALWAYS_INLINE void fthread_state_execution_write_locked(fthread_t* thread, fthread_state_execution_t execution_state) {
	thread->state = (thread->state & ~fthread_state_execution_mask) | (execution_state & fthread_state_execution_mask);
};

/**
 * Informs the threads subsystem that an interrupt occurred while the given thread was current.
 */
void fthread_interrupt_start(fthread_t* thread);

/**
 * Informs the threads subsystem that an interrupt has ended while the given thread was current.
 *
 * @note This is not necessarily the same thread given to `fthread_interrupt_start`.
 */
void fthread_interrupt_end(fthread_t* thread);

/**
 * Informs the threads subsystem that the given thread has died.
 *
 * This MUST NOT be called in the context of the thread. For example, if called within an interrupt context, it MUST have a separate stack from the thread's stack.
 */
void fthread_died(fthread_t* thread);

/**
 * Initializes the given thread with architecture-specific information.
 */
void farch_thread_init_info(fthread_t* thread, fthread_initializer_f initializer, void* data);

/**
 * Similar to `fthread_wait`, but the waitq is already locked.
 *
 * If the function fails, it returns with the waitq still locked.
 * However, if it succeeds, the lock will be held until the thread is fully suspended (which may already be the case). It will not drop it at all until this occurs.
 *
 * @note If the thread is already waiting for a waitq, this function may produce a deadlock if someone else is holding the lock for that old waitq and wants to lock this new waitq.
 *       This deadlock is not possible with `fthread_wait`.
 */
FERRO_WUR ferr_t fthread_wait_locked(fthread_t* thread, fwaitq_t* waitq);

FERRO_DECLARATIONS_END;

#endif // _FERRO_CORE_THREADS_PRIVATE_H_
