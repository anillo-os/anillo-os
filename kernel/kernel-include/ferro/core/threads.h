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
 * Threads subsystem.
 */

#ifndef _FERRO_CORE_THREADS_H_
#define _FERRO_CORE_THREADS_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include <ferro/base.h>
#include <ferro/platform.h>
#include <ferro/error.h>
#include <ferro/core/locks.h>
#include <ferro/core/waitq.h>
#include <ferro/core/timers.h>
#include <ferro/core/refcount.h>

// include the arch-dependent before-header
#if FERRO_ARCH == FERRO_ARCH_x86_64
	#include <ferro/core/x86_64/threads.before.h>
#elif FERRO_ARCH == FERRO_ARCH_aarch64
	#include <ferro/core/aarch64/threads.before.h>
#else
	#error Unrecognized/unsupported CPU architecture! (see <ferro/core/threads.h>)
#endif

FERRO_DECLARATIONS_BEGIN;

/**
 * @addtogroup Threads
 *
 * The threading subsystem.
 *
 * @{
 */

// only the first 32 bits can be used for public flags
FERRO_OPTIONS(uint64_t, fthread_flags) {
	/**
	 * Deallocate the stack using the paging subsystem when the thread exits.
	 */
	fthread_flag_deallocate_stack_on_exit = 1 << 0,

	/**
	 * Indicates that the exit data stored by the thread was copied using the mempool subsystem and should be freed when appropriate.
	 */
	fthread_flag_exit_data_copied         = 1 << 1,
};

FERRO_ENUM(uint8_t, fthread_state_execution) {
	/**
	 * Indicates that the thread is not currently running but it is available to run again whenever possible.
	 */
	fthread_state_execution_not_running = 0,

	/**
	 * Indicates that the thread is not currently running and should not run again until it is manually resumed.
	 */
	fthread_state_execution_suspended   = 1,

	/**
	 * Indicates that the thread is currently running.
	 */
	fthread_state_execution_running     = 2,

	/**
	 * Indicates that the thread is dead. It must never run again.
	 */
	fthread_state_execution_dead        = 3,

	/**
	 * Indicates that the thread was running when the current interrupt occurred.
	 */
	fthread_state_execution_interrupted = 4,
};

FERRO_OPTIONS(uint64_t, fthread_state) {
	fthread_state_execution_mask     = 7 << 0,
	fthread_state_pending_suspend    = 1 << 3,
	fthread_state_pending_death      = 1 << 4,
	fthread_state_holding_waitq_lock = 1 << 5,

	/**
	 * Indicates that this thread has been interrupted (e.g. by a signal).
	 *
	 * This is mainly used by userspace-support code to indicate that a signal
	 * has arrived and the thread should try to exit kernel-space as soon as possible.
	 */
	fthread_state_interrupted = 1 << 6,

	/**
	 * Indicates that this thread is blocked and cannot not be scheduled to run.
	 */
	fthread_state_blocked = 1 << 7,

	fthread_state_pending_block = 1 << 8,

	// these are just to shut Clang up

	fthread_state_execution_mask_bit_1 = 1 << 0,
	fthread_state_execution_mask_bit_2 = 1 << 1,
	fthread_state_execution_mask_bit_3 = 1 << 2,
};

FERRO_ENUM(uint8_t, fthread_timeout_type) {
	/**
	 * The timeout value is a relative duration in nanoseconds. Once the given number of nanoseconds have elapsed, the timeout fires.
	 */
	fthread_timeout_type_ns_relative,

	/**
	 * The timeout value is an absolute number of nanoseconds, with 0 being the start of the monotonic clock. Once the monotonic clock reaches the given value, the timeout fires.
	 *
	 * @note Because timeouts are only scheduled once the thread fully suspends, absolute timeouts might fire immediately.
	 *
	 * @todo This timeout type is not currently supported.
	 */
	fthread_timeout_type_ns_absolute_monotonic,
};

typedef uint64_t fthread_id_t;

#define FTHREAD_ID_INVALID UINT64_MAX

FERRO_STRUCT(fthread) {
	/**
	 * #prev and #next are owned by the thread manager responsible for this thread.
	 * They cannot be safely read or written by anyone else.
	 */
	fthread_t* prev;
	/**
	 * @see #prev.
	 */
	fthread_t* next;

	fthread_flags_t flags;
	fthread_state_t state;
	void* stack_base;
	size_t stack_size;

	/**
	 * Number of references held on this thread. If this drops to `0`, the thread is released.
	 *
	 * This MUST be accessed and modified ONLY with fthread_retain() and fthread_release().
	 */
	frefcount_t reference_count;

	/**
	 * Protects #flags, #state, #exit_data (and #exit_data_size), #saved_context, #wait_link, and #pending_waitq from being read or written.
	 */
	flock_spin_intsafe_t lock;

	/**
	 * Data passed by the thread upon exit. May be `NULL`.
	 */
	void* exit_data;

	/**
	 * Size of #exit_data.
	 */
	size_t exit_data_size;

	/**
	 * Architecture-dependent structure containing context information from the last suspension of the thread.
	 */
	fthread_saved_context_t* saved_context;

	/**
	 * Used to link this thread to onto a list of waiters waiting for a waitq to wake up.
	 * Only used when the thread is suspended while waiting for a waitq.
	 */
	fwaitq_waiter_t wait_link;

	/**
	 * Used when suspending a thread due to waiting for a waitq.
	 *
	 * If a suspension is currently pending and this is not `NULL`, this is a waitq to add the thread onto once it is fully suspended. This is meant to be done by the thread's manager.
	 *
	 * If the thread is currently suspended and this is not `NULL`, this is the waitq that the thread is currently waiting for.
	 */
	fwaitq_t* waitq;

	/**
	 * Assigned by the thread manager when it starts managing the thread.
	 */
	fthread_id_t id;

	/**
	 * A waitq used to wait for the thread to die.
	 *
	 * @note The thread pointer is still valid when these waiters are notified and can still be retained.
	 *
	 * @note The waiters are notified from within a worker.
	 */
	fwaitq_t death_wait;

	/**
	 * A waitq used to wait for the thread to be destroyed.
	 *
	 * @note The thread pointer is still valid when these waiters are notified but can no longer be retained.
	 *       These waiters are notified before resource deallocation begins.
	 *
	 * @note The waiters are notified from within a worker.
	 */
	fwaitq_t destroy_wait;

	/**
	 * A waitq used to wait for the thread to be suspended.
	 *
	 * @note The waiters are notified from any execution context. It may be a worker, but it may be another thread or even an interrupt.
	 */
	fwaitq_t suspend_wait;

	fwaitq_t block_wait;

	uint64_t block_count;
};

/**
 * The first function to be executed when a thread is started.
 *
 * @param data User-defined data given to fthread_new() upon thread creation.
 */
typedef void (*fthread_initializer_f)(void* data);

void fthread_init(void);

// these are arch-dependent functions we expect all architectures to implement

/**
 * Allocates and initializes a new thread with the given information.
 *
 * @param initializer The first function to call when the thread is initialized.
 * @param data        User-defined data to pass to the thread initializer.
 * @param stack_base  The base (i.e. lowest address) of the stack for the thread. If this is `NULL`, a stack is immediately allocated and ::fthread_flag_deallocate_stack_on_exit will automatically be set in the thread flags.
 * @param stack_size  The size of the stack for the thread.
 * @param flags       Flags to assign to the thread.
 * @param[out] out_thread  A pointer in which a pointer to the newly allocated thread is written.
 *
 * @note The newly created thread is suspended on creation. However, in order to start it, it must first be assigned to a thread manager (like the scheduler subsystem). Then, it can be resumed with fthread_resume().
 *
 * @note All threads must start in kernel-space. They can switch to user-space later if necessary.
 *
 * @note The threads subsystem and/or thread manager may need to use part of the stack before the initializer is called.
 *
 * @note The caller is granted a single reference to the new thread.
 *
 * Return values:
 * @retval ferr_ok               The thread was successfully allocated and initialized.
 * @retval ferr_invalid_argument One or more of: 1) @p initializer was `NULL`, 2) @p flags contained an invalid value, 3) @p out_thread was `NULL`.
 * @retval ferr_temporary_outage One or more of: 1) there were insufficient resources to allocate a new thread structure, 2) if @p stack_base was `NULL`, indicates there was not enough memory to allocate a stack.
 */
FERRO_WUR ferr_t fthread_new(fthread_initializer_f initializer, void* data, void* stack_base, size_t stack_size, fthread_flags_t flags, fthread_t** out_thread);

/**
 * Retrieves a pointer to the thread information structure for the thread that is currently executing on the current CPU.
 *
 * The returned pointer MAY be `NULL` if there is no active thread on the current CPU.
 *
 * However, in an interrupt context, this will return the thread that was executing when the interrupt occurred.
 *
 * @note This function DOES NOT grant a reference on the thread. However, because this returns the *current* thread, callers can rest assured that the thread *is* valid.
 */
fthread_t* fthread_current(void);

/**
 * Exits the current thread. MUST be called within a thread context, NOT an interrupt context.
 *
 * @param exit_data      Optional pointer to some data to save to thread information structure upon exit.
 * @param exit_data_size Size of the data pointed to by @p exit_data.
 * @param copy_exit_data Whether to copy the exit data using the mempool subsystem before storing it. If this is `true`, ::fthread_flag_exit_data_copied is automatically set on the thread.
 *
 * @note If @p copy_exit_data is `true` but there are insufficient resources to copy the data, the exit data is not stored and ::fthread_flag_exit_data_copied is not set.
 */
FERRO_NO_RETURN void fthread_exit(void* exit_data, size_t exit_data_size, bool copy_exit_data);

/**
 * Suspends the given thread.
 *
 * @param thread The thread to suspend. If this is `NULL`, the current thread is used.
 * @param wait   If `true`, this function will not return until the thread has been suspended.
 *               If `false`, this function will request that the given thread be suspended;
 *               however, it may not be suspended yet upon return.
 *
 * @note If you suspend your own thread (i.e. the one that is currently running), execution is immediately stopped. It will always succeed in this case.
 *
 * Return values:
 * @retval ferr_ok                  The thread was previously resumed and has now been successfully suspended.
 * @retval ferr_already_in_progress The thread was already suspended (or marked for suspension) and was not affected by this call.
 * @retval ferr_permanent_outage    The thread was dead (or had an imminent death).
 * @retval ferr_invalid_argument    The thread had no registered manager.
 */
FERRO_WUR ferr_t fthread_suspend(fthread_t* thread, bool wait);

/**
 * Like fthread_suspend(), but once suspended, starts a timer to resume the thread.
 *
 * @param thread        The thread to suspend. If this is `NULL`, the current thread is used.
 * @param timeout_value The value for the timer. How this value is interpreted depends on @p timeout_origin. A value of 0 for this disables the timeout, no matter what timeout type is specified.
 * @param timeout_type  This determines how the timeout value is interpreted. See ::fthread_timeout_origin for details on what each value does.
 * @param wait          If `true`, this function will not return until the thread has been suspended.
 *                      If `false`, this function will request that the given thread be suspended;
 *                      however, it may not be suspended yet upon return.
 *
 * @note The timer is started once the thread is suspended, not before.
 *
 * Return values:
 * @retval ferr_ok                  The thread was previously resumed and has now been successfully suspended.
 * @retval ferr_already_in_progress The thread was already suspended (or marked for suspension) and was not affected by this call.
 * @retval ferr_permanent_outage    The thread was dead (or had an imminent death).
 * @retval ferr_invalid_argument    The thread had no registered manager.
 */
FERRO_WUR ferr_t fthread_suspend_timeout(fthread_t* thread, bool wait, uint64_t timeout_value, fthread_timeout_type_t timeout_type);

/**
 * Prevents the given thread from running again until it is unblocked.
 *
 * @param thread The thread to block.
 * @param wait   If `true`, this function will not return until the given thread has stopped
 *               running and has been fully blocked.
 *               If `false`, this function will only mark the thread as blocked from running and
 *               interrupt it if it is currently running, but it will not wait for it to actually stop.
 *
 * @note Threads keep track of the number of blocks placed on them and will not become available to run
 *       until they are unblocked.
 *
 * @note Threads that are blocked can still be suspended, resumed, or killed.
 *       The only difference is that, if the scheduler managing them tries to schedule a blocked thread,
 *       it will see the thread is blocked and avoid scheduling it.
 */
FERRO_WUR ferr_t fthread_block(fthread_t* thread, bool wait);
FERRO_WUR ferr_t fthread_unblock(fthread_t* thread);

/**
 * Suspends the current thread.
 *
 * @note This is a convenience wrapper around fthread_suspend().
 */
void fthread_suspend_self(void);

/**
 * Resumes the given thread.
 *
 * @param thread The thread to resume.
 *
 * @note Resumption might not occur immediately upon invocation of this function.
 *
 * Return values:
 * @retval ferr_ok                  The thread was previously suspended and has now been successfully resumed.
 * @retval ferr_already_in_progress The thread was already resumed (or marked for resumption) and was not affected by this call.
 * @retval ferr_permanent_outage    The thread was dead (or had an imminent death).
 * @retval ferr_invalid_argument    The thread was `NULL` or had no registered manager.
 */
FERRO_WUR ferr_t fthread_resume(fthread_t* thread);

/**
 * Kills the given thread.
 *
 * @param thread The thread to kill. If `NULL`, the current thread is used.
 *
 * @note If you kill your own thread (i.e. the one that is currently running), execution is immediately stopped. It will always succeed in this case.
 *       In this case, this function also behaves as if it were `FERRO_NO_RETURN`.
 *
 * @note Killing a thread is a one-way operation. Once it's set in motion, it cannot be stopped.
 *
 * @note If the caller does not hold their own reference on the thread (i.e. the only reference on the thread is from the thread manager),
 *       the thread may be fully released by this operation. To ensure valid access to the thread after this operation is performed, retain the thread beforehand.
 *
 * Return values:
 * @retval ferr_ok                  The thread was successfully killed (but death may happen later).
 * @retval ferr_already_in_progress The thread was already dead (or marked for death) and was not affected by this call.
 * @retval ferr_invalid_argument    The thread had no registered manager or was fully released before this operation could complete.
 */
FERRO_WUR ferr_t fthread_kill(fthread_t* thread);

/**
 * Kills the current thread.
 *
 * @note This is a convenience wrapper around fthread_kill(). It also tells the compiler that this function should never return and thus allow it to make further optimizations/sanity checks.
 */
FERRO_NO_RETURN void fthread_kill_self(void);

/**
 * Tries to retain the given thread.
 *
 * @param thread The thread to try to retain.
 *
 * Return values:
 * @retval ferr_ok               The thread was successfully retained.
 * @retval ferr_permanent_outage The thread was deallocated while this call occurred. It is no longer valid.
 */
FERRO_WUR ferr_t fthread_retain(fthread_t* thread);

/**
 * Releases the given thread.
 *
 * @param thread The thread to release.
 */
void fthread_release(fthread_t* thread);

/**
 * Retrieves the given thread's current execution state at the time of the call.
 *
 * @param thread The thread whose current execution state will be retrieved.
 *
 * @note The thread's execution state may have already changed when this call returns.
 *       The only state in which the thread will not change to any other state is fthread_state_execution_dead().
 */
fthread_state_execution_t fthread_execution_state(fthread_t* thread);

/**
 * Suspends the given thread and adds it as a waiter on the given waitq. When the waitq wakes the thread, the thread will resume.
 *
 * @param thread The thread to suspend. If this is `NULL`, the current thread is used.
 * @param waitq  The waitq to wait for.
 *
 * @note This function locks the waitq and holds it locked until the thread is either fully suspended or the suspension is cancelled/interrupted (e.g. by a call to fthread_resume()).
 *
 * @note If you suspend your own thread (i.e. the one that is currently running), execution is immediately stopped. It will always succeed in this case.
 *
 * @note A thread can only wait for a single waitq at a time. If the thread was already suspended and waiting for a different waitq,
 *       it will be removed from the previous waitq's waiting list and added onto the new waitq's waiting list.
 *
 * @note The thread may be resumed externally (e.g. with fthread_resume()) before the waitq wakes it up. In this case, the thread will stop waiting for the waitq and simply resume.
 *       Thus, waiting for a waitq may result in seemingly-spurious wakeups from the thread's point-of-view.
 *
 * Return values:
 * @retval ferr_ok                  The thread was either 1) previously resumed and marked for suspension, or 2) already suspended. In either case, once suspended, the thread will be placed onto the waitq's waiting list.
 * @retval ferr_permanent_outage    The thread was dead (or had an imminent death).
 * @retval ferr_invalid_argument    The thread had no registered manager.
 */
FERRO_WUR ferr_t fthread_wait(fthread_t* thread, fwaitq_t* waitq);

/**
 * Like fthread_wait(), but once the thread begins waiting, starts a timer to resume the thread.
 *
 * @note Unlike fthread_suspend_timeout(), this function WILL overwrite any pending timeout.
 */
FERRO_WUR ferr_t fthread_wait_timeout(fthread_t* thread, fwaitq_t* waitq, uint64_t timeout_value, fthread_timeout_type_t timeout_type);

FERRO_ALWAYS_INLINE bool fthread_saved_context_is_kernel_space(fthread_saved_context_t* saved_context);

void fthread_mark_interrupted(fthread_t* thread);
void fthread_unmark_interrupted(fthread_t* thread);
FERRO_WUR bool fthread_marked_interrupted(fthread_t* thread);

/**
 * @}
 */

FERRO_DECLARATIONS_END;

// include the arch-dependent after-header
#if FERRO_ARCH == FERRO_ARCH_x86_64
	#include <ferro/core/x86_64/threads.after.h>
#elif FERRO_ARCH == FERRO_ARCH_aarch64
	#include <ferro/core/aarch64/threads.after.h>
#else
	#error Unrecognized/unsupported CPU architecture! (see <ferro/core/threads.h>)
#endif

#endif // _FERRO_CORE_THREADS_H_
