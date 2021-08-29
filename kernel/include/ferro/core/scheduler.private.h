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

#ifndef _FERRO_CORE_SCHEDULER_PRIVATE_H_
#define _FERRO_CORE_SCHEDULER_PRIVATE_H_

#include <ferro/base.h>
#include <ferro/platform.h>
#include <ferro/core/scheduler.h>
#include <ferro/core/threads.h>
#include <ferro/core/locks.h>
#include <ferro/core/timers.h>

FERRO_DECLARATIONS_BEGIN;

FERRO_STRUCT(fsched_info) {
	// protects the structure contents from being read or written
	flock_spin_intsafe_t lock;

	// the head of the circular queue for the threads eligible to run on this CPU
	fthread_t* head;
	// the tail of the circular queue for the thread eligible to run on this CPU
	fthread_t* tail;

	// how many threads are in the circular queue
	size_t count;

	// the ID of the last-armed timer
	ftimers_id_t last_timer_id;
};

/**
 * Arms the preemption timer.
 */
void fsched_arm_timer(void);

/**
 * Disarms the preemption timer.
 */
void fsched_disarm_timer(void);

/**
 * Returns a pointer to the scheduler information structure for the current CPU.
 */
fsched_info_t* fsched_per_cpu_info(void);

// these are arch-dependent functions we expect all architectures to implement

/**
 * The core of the context-switching logic.
 *
 * @param current_thread The thread that is currently active. May be `NULL`.
 * @param new_thread     The thread to switch to.
 *
 * @note This function may or may not be called from an interrupt context. Arch-dependent implementations need to be aware of this and adapt.
 *
 * @note This function MUST arm the timer as well (with `fsched_arm_timer`).
 *
 * @note If `current_thread` is `NULL`, this function should not save the current context. It should only load the new context.
 *
 * @note `current_thread` and `new_thread` might be the same thread. In that case, all this function needs to do is arm the timer.
 *       However, this need not be a separate behavior. As long as the implementation can properly handle the two threads being equal, it doesn't matter if this is handled as a separate case or not.
 */
void fsched_switch(fthread_t* current_thread, fthread_t* new_thread);

/**
 * Called to bootstrap the scheduler upon initialization.
 *
 * @param thread The thread to switch to.
 *
 * @note This function does not return to its caller. It switches to the destination thread and continues execution there.
 *
 * @note This function MUST arm the timer as well (with `fsched_arm_timer`).
 *
 * @note This function WILL NOT be called from an interrupt context and implementations may assume this is true.
 */
FERRO_NO_RETURN void fsched_bootstrap(fthread_t* thread);

/**
 * Performs architecture-specific scheduler initialization. Called at the start of the main scheduler initialization code.
 */
void farch_sched_init(void);

/**
 * Tells the scheduler that the given thread needs to be preempted as soon as possible.
 *
 * @note This function does not need to wait for the thread to be preempted.
 *
 * @note This function will be called with the thread's lock NOT held.
 *
 * @note If the given thread is the current thread, this function MUST NOT return.
 */
void fsched_preempt_thread(fthread_t* thread);

FERRO_DECLARATIONS_END;

// now include the arch-dependent header
#if FERRO_ARCH == FERRO_ARCH_x86_64
	#include <ferro/core/x86_64/scheduler.private.h>
#elif FERRO_ARCH == FERRO_ARCH_aarch64
	#include <ferro/core/aarch64/scheduler.private.h>
#else
	#error Unrecognized/unsupported CPU architecture! (see <ferro/core/scheduler.private.h>)
#endif

#endif // _FERRO_CORE_SCHEDULER_PRIVATE_H_
