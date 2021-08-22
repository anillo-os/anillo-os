/**
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

FERRO_DECLARATIONS_END;

#endif // _FERRO_CORE_THREADS_PRIVATE_H_
