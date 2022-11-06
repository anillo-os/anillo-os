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
 * Threads subsystem; private components.
 */

#ifndef _FERRO_CORE_THREADS_PRIVATE_H_
#define _FERRO_CORE_THREADS_PRIVATE_H_

#include <stdint.h>
#include <stddef.h>

#include <ferro/base.h>
#include <ferro/core/threads.h>

FERRO_DECLARATIONS_BEGIN;

/**
 * @addtogroup Threads
 *
 * @{
 */

/**
 * Requests that the given thread be suspended as soon as possible.
 *
 * @note Called with the thread lock held.
 *
 * @retval ferr_ok               The request was handled and lower hooks may still be invoked.
 * @retval ferr_permanent_outage The request was handled and lower hooks may NOT be invoked.
 * @retval ferr_unknown          The request was not handled; lower hooks (if any) will be invoked.
 */
typedef ferr_t (*fthread_hook_suspend_f)(void* context, fthread_t* thread);

/**
 * Requests that the given thread be resumed as soon as possible.
 *
 * @note Called with the thread lock held.
 *
 * @retval ferr_ok               The request was handled and lower hooks may still be invoked.
 * @retval ferr_permanent_outage The request was handled and lower hooks may NOT be invoked.
 * @retval ferr_unknown          The request was not handled; lower hooks (if any) will be invoked.
 */
typedef ferr_t (*fthread_hook_resume_f)(void* context, fthread_t* thread);

/**
 * Requests that the given thread be killed as soon as possible.
 *
 * @note Called with the thread lock held.
 *
 * @retval ferr_ok               The request was handled and lower hooks may still be invoked.
 * @retval ferr_permanent_outage The request was handled and lower hooks may NOT be invoked.
 * @retval ferr_unknown          The request was not handled; lower hooks (if any) will be invoked.
 */
typedef ferr_t (*fthread_hook_kill_f)(void* context, fthread_t* thread);

/**
 * Requests that the given thread be blocked as soon as possible.
 *
 * @note Called with the thread lock held.
 *
 * @retval ferr_ok               The request was handled and lower hooks may still be invoked.
 * @retval ferr_permanent_outage The request was handled and lower hooks may NOT be invoked.
 * @retval ferr_unknown          The request was not handled; lower hooks (if any) will be invoked.
 */
typedef ferr_t (*fthread_hook_block_f)(void* context, fthread_t* thread);

/**
 * Requests that the given thread be unblocked as soon as possible.
 *
 * @note Called with the thread lock held.
 *
 * @retval ferr_ok               The request was handled and lower hooks may still be invoked.
 * @retval ferr_permanent_outage The request was handled and lower hooks may NOT be invoked.
 * @retval ferr_unknown          The request was not handled; lower hooks (if any) will be invoked.
 */
typedef ferr_t (*fthread_hook_unblock_f)(void* context, fthread_t* thread);

/**
 * Informs the hook that the given thread is entering an interrupt.
 *
 * @note Called with the thread lock NOT held.
 *
 * @retval ferr_ok               The request was handled and lower hooks may still be invoked.
 * @retval ferr_permanent_outage The request was handled and lower hooks may NOT be invoked.
 * @retval ferr_unknown          The request was not handled; lower hooks (if any) will be invoked.
 */
typedef ferr_t (*fthread_hook_interrupted_f)(void* context, fthread_t* thread);

/**
 * Informs the hook that the given thread is returning from an interrupt.
 *
 * @note Called with the thread lock NOT held.
 *
 * @retval ferr_ok               The request was handled and lower hooks may still be invoked.
 * @retval ferr_permanent_outage The request was handled and lower hooks may NOT be invoked.
 * @retval ferr_unknown          The request was not handled; lower hooks (if any) will be invoked.
 */
typedef ferr_t (*fthread_hook_ending_interrupt_f)(void* context, fthread_t* thread);

/**
 * Allows the hook to handle a bus error on the given thread.
 *
 * @note Called with the thread lock NOT held.
 *
 * If none of the thread's hooks are able able to handle the bus error, the kernel panics.
 *
 * @retval ferr_ok               The request was handled and lower hooks may still be invoked.
 * @retval ferr_permanent_outage The request was handled and lower hooks may NOT be invoked.
 * @retval ferr_unknown          The request was not handled; lower hooks (if any) will be invoked.
 */
typedef ferr_t (*fthread_hook_bus_error_f)(void* context, fthread_t* thread, void* address);

/**
 * Allows the hook to handle a page fault on the given thread.
 *
 * @note Called with the thread lock NOT held.
 *
 * If none of the thread's hooks are able able to handle the page fault, the kernel panics.
 *
 * @retval ferr_ok               The request was handled and lower hooks may still be invoked.
 * @retval ferr_permanent_outage The request was handled and lower hooks may NOT be invoked.
 * @retval ferr_unknown          The request was not handled; lower hooks (if any) will be invoked.
 */
typedef ferr_t (*fthread_hook_page_fault_f)(void* context, fthread_t* thread, void* address);

/**
 * Allows the hook to handle a floating point exception on the given thread.
 *
 * @note Called with the thread lock NOT held.
 *
 * If none of the thread's hooks are able able to handle the exception, the kernel panics.
 *
 * @retval ferr_ok               The request was handled and lower hooks may still be invoked.
 * @retval ferr_permanent_outage The request was handled and lower hooks may NOT be invoked.
 * @retval ferr_unknown          The request was not handled; lower hooks (if any) will be invoked.
 */
typedef ferr_t (*fthread_hook_floating_point_exception_f)(void* context, fthread_t* thread);

/**
 * Allows the hook to handle an illegal instruction on the given thread.
 *
 * @note Called with the thread lock NOT held.
 *
 * If none of the thread's hooks are able able to handle the illegal instruction, the kernel panics.
 *
 * @retval ferr_ok               The request was handled and lower hooks may still be invoked.
 * @retval ferr_permanent_outage The request was handled and lower hooks may NOT be invoked.
 * @retval ferr_unknown          The request was not handled; lower hooks (if any) will be invoked.
 */
typedef ferr_t (*fthread_hook_illegal_instruction_f)(void* context, fthread_t* thread);

/**
 * Allows the hook to handle a debug trap on the given thread.
 *
 * @note Called with the thread lock NOT held.
 *
 * If none of the thread's hooks are able able to handle the debug trap, the kernel panics.
 *
 * @retval ferr_ok               The request was handled and lower hooks may still be invoked.
 * @retval ferr_permanent_outage The request was handled and lower hooks may NOT be invoked.
 * @retval ferr_unknown          The request was not handled; lower hooks (if any) will be invoked.
 */
typedef ferr_t (*fthread_hook_debug_trap_f)(void* context, fthread_t* thread);

FERRO_STRUCT(fthread_hook_callbacks) {
	fthread_hook_suspend_f suspend;
	fthread_hook_resume_f resume;
	fthread_hook_kill_f kill;
	fthread_hook_block_f block;
	fthread_hook_unblock_f unblock;
	fthread_hook_interrupted_f interrupted;
	fthread_hook_ending_interrupt_f ending_interrupt;
	fthread_hook_bus_error_f bus_error;
	fthread_hook_page_fault_f page_fault;
	fthread_hook_floating_point_exception_f floating_point_exception;
	fthread_hook_illegal_instruction_f illegal_instruction;
	fthread_hook_debug_trap_f debug_trap;
};

/**
 * Thread hooks are a way of intercepting certain actions/events for a thread.
 *
 * Thread hooks are invoked in order of precedence, with hook 0 having the highest precedence.
 *
 * Hook 0 is reserved for thread managers.
 *
 * All hook functions are optional.
 */
FERRO_STRUCT(fthread_hook) {
	/**
	 * A hook-defined context argument to pass to all the hook functions when they're invoked.
	 */
	void* context;

	/**
	 * A unique hook owner ID that no other hook owner has.
	 */
	uint64_t owner_id;

	fthread_hook_suspend_f suspend;
	fthread_hook_resume_f resume;
	fthread_hook_kill_f kill;
	fthread_hook_block_f block;
	fthread_hook_unblock_f unblock;
	fthread_hook_interrupted_f interrupted;
	fthread_hook_ending_interrupt_f ending_interrupt;
	fthread_hook_bus_error_f bus_error;
	fthread_hook_page_fault_f page_fault;
	fthread_hook_floating_point_exception_f floating_point_exception;
	fthread_hook_illegal_instruction_f illegal_instruction;
	fthread_hook_debug_trap_f debug_trap;
};

FERRO_OPTIONS(uint64_t, fthread_private_flags) {
	// only the last 32 bits can be used for private flags

	/**
	 * Indicates that this thread does have a userspace context.
	 *
	 * This is a hack in terms of modularization.
	 * Ideally, the core thread code should have NO knowledge of the userspace context code built on top of it.
	 * However, for efficiency purposes, it is very useful to have this information readily accessible without having to look it up in a hashmap or anything like that.
	 */
	fthread_private_flag_has_userspace = 1 << 32,
};

FERRO_STRUCT(fthread_private) {
	fthread_t thread;

	uint64_t pending_timeout_value;
	fthread_timeout_type_t pending_timeout_type;
	ftimers_id_t timer_id;

	/**
	 * A bitmap indicating which hooks are in-use. e.g. Bit 0 corresponds to slot 0.
	 *
	 * Protected by the thread lock.
	 *
	 * Generally, once a hook is registered, it is not unregistered. In fact, unregistering a hook is racy and unsafe.
	 */
	uint8_t hooks_in_use;
	fthread_hook_t hooks[4];
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
 * @note This is not necessarily the same thread given to fthread_interrupt_start().
 */
void fthread_interrupt_end(fthread_t* thread);

/**
 * Informs the threads subsystem that the given thread has died.
 *
 * This MUST NOT be called in the context of the thread. For example, if called within an interrupt context, it MUST have a separate stack from the thread's stack.
 */
void fthread_died(fthread_t* thread);

/**
 * Informs the threads subsystem that the given thread has been suspended.
 */
void fthread_suspended(fthread_t* thread);

void fthread_blocked(fthread_t* thread);

/**
 * Initializes the given thread with architecture-specific information.
 */
void farch_thread_init_info(fthread_t* thread, fthread_initializer_f initializer, void* data);

/**
 * Similar to fthread_wait(), but the waitq is already locked.
 *
 * If the function fails, it returns with the waitq still locked.
 * However, if it succeeds, the lock will be held until the thread is fully suspended (which may already be the case). It will not drop it at all until this occurs.
 *
 * @note If the thread is already waiting for a waitq, this function may produce a deadlock if someone else is holding the lock for that old waitq and wants to lock this new waitq.
 *       This deadlock is not possible with fthread_wait().
 */
FERRO_WUR ferr_t fthread_wait_locked(fthread_t* thread, fwaitq_t* waitq);

FERRO_WUR ferr_t fthread_wait_timeout_locked(fthread_t* thread, fwaitq_t* waitq, uint64_t timeout_value, fthread_timeout_type_t timeout_type);

uint8_t fthread_register_hook(fthread_t* thread, uint64_t owner_id, void* context, const fthread_hook_callbacks_t* callbacks);

uint8_t fthread_find_hook(fthread_t* thread, uint64_t owner_id);

/**
 * @}
 */

FERRO_DECLARATIONS_END;

#endif // _FERRO_CORE_THREADS_PRIVATE_H_
