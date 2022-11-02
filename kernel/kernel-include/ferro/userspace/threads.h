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
 * Userspace Threads subsystem.
 */

#ifndef _FERRO_USERSPACE_THREADS_H_
#define _FERRO_USERSPACE_THREADS_H_

#include <ferro/core/threads.h>
#include <ferro/core/paging.h>

FERRO_DECLARATIONS_BEGIN;

FERRO_STRUCT_FWD(fproc);

/**
 * A custom syscall handler for a uthread.
 *
 * Syscall handlers run the kernel-space context of the uthread for which they are registered.
 * As such, they can perform all normal thread operations and have interrupts enabled (and are preempted as threads normally are).
 *
 * @param context      The context given to futhread_register along with this function.
 * @param uthread      The uthread that made the syscall. This is also returned by fthread_current().
 * @param user_context The saved userspace context of the uthread that will be loaded whe returning from the syscall.
 *                     This can be freely modified to change the context upon return.
 *                     Note that rcx and r11 are clobbered by syscalls, so reading from or writing to those will have no effect.
 *                     This can also be accessed through futhread_context().
 */
typedef void (*futhread_syscall_handler_f)(void* context, fthread_t* uthread, fthread_saved_context_t* user_context);

FERRO_OPTIONS(uint64_t, futhread_flags) {
	/**
	 * Deallocate the user stack using the paging subsystem when the uthread exits.
	 */
	futhread_flag_deallocate_user_stack_on_exit    = 1 << 0,

	/**
	 * Destroy the user address space (with fpage_space_destroy()) when the uthread exits.
	 *
	 * @note If the user address space provided when registering the thread was in allocated memory, that memory is NOT deallocated automatically.
	 *       You MUST register a waiter to wait for the uthread to die and then release the memory there.
	 */
	futhread_flag_destroy_address_space_on_exit    = 1 << 1,

	/**
	 * Deallocate the user address space (with fmempool_free()) when the uthread exits.
	 *
	 * @note If the user address space was not allocated with the mempool subsystem, this flag CANNOT be used.
	 */
	futhread_flag_deallocate_address_space_on_exit = 1 << 2,
};

/**
 * A data structure that is associated with a thread when it's registered as a uthread.
 *
 * Userspace threads are ones that, in addition to having a kernel-space context,
 * also have a userspace context in which userspace code can run.
 *
 * UThreads are created from existing kernel-space threads.
 *
 * UThreads will automatically manage system call interfacing with the interrupts subsystem.
 * When a system call is received from userspace, UThreads invoke the system call handler for the thread.
 * The default handler simply generates an exception, but this can be changed by specifying a syscall handler in futhread_register().
 *
 * As mentioned earlier, uthreads start in kernel-space. Therefore, to enter userspace, they must
 * manually do so using futhread_jump_user(). This function can be called at any time to switch the given
 * uthread into userspace, continuing execution at the given address.
 *
 * UThread data shares its lifetime with that of its thread.
 */
FERRO_STRUCT(futhread_data) {
	futhread_flags_t flags;

	/**
	 * The user address space for this uthread.
	 */
	fpage_space_t* user_space;

	void* user_stack_base;
	size_t user_stack_size;

	/**
	 * A waitq waiter used to wait for the thread's death.
	 */
	fwaitq_waiter_t thread_death_waiter;

	/**
	 * A waitq waiter used to wait for the thread's destruction.
	 */
	fwaitq_waiter_t thread_destruction_waiter;

	fthread_saved_context_t* saved_syscall_context;

	futhread_syscall_handler_f syscall_handler;
	void* syscall_handler_context;

	/**
	 * A waitq used to wait for the uthread to die.
	 *
	 * @note This is different from the thread's death waitq.
	 *       The waiters for thread death are notified when the thread dies; the waiters for uthread death are notified when the uthread dies.
	 *       Which one to use depends on what you need: if you need something related to the uthread (e.g. to clean up some user data or maybe the address space),
	 *       wait for uthread death. Otherwise, wait for thread death.
	 *
	 * @note The thread pointer for this uthread is still valid when these waiters are notified.
	 *
	 * @note The waiters are notified from within a worker.
	 */
	fwaitq_t death_wait;

	/**
	 * A waitq used to wait for the uthread to be destroyed.
	 *
	 * @note The thread pointer for this uthread is still valid when these waiters are notified but can no longer be retained.
	 *       These waiters are notified before resource deallocation begins.
	 *
	 * @note The waiters are notified from within a worker.
	 */
	fwaitq_t destroy_wait;
};

/**
 * Returns `true` if the given thread is a uthread or `false` if it is not.
 */
bool fthread_is_uthread(fthread_t* thread);

/**
 * Returns a reference to the user address space for the given uthread.
 *
 * @retval ferr_ok               A reference to the uthread's user address space was successfully written into @p out_space.
 * @retval ferr_invalid_argument One or more of: 1) the given thread was invalid (e.g. not a uthread), 2) @p out_space was `NULL`.
 */
FERRO_WUR ferr_t futhread_space(fthread_t* uthread, fpage_space_t** out_space);

/**
 * Returns a reference to the saved userspace context for the given uthread.
 *
 * @retval ferr_ok               A reference to the uthread's saved userspace context was successfully written into @p out_saved_user_context.
 * @retval ferr_invalid_argument One or more of: 1) the given thread was invalid (e.g. not a uthread), 2) @p out_saved_user_context was `NULL`.
 */
FERRO_WUR ferr_t futhread_context(fthread_t* uthread, fthread_saved_context_t** out_saved_user_context);

/**
 * Allocates and initializes a new uthread with the given information.
 *
 * @param thread                  The thread to register as a uthread.
 * @param user_stack_base         The base (i.e. lowest address) of the user stack for the uthread. If this is `NULL`, a stack is immediately allocated and ::futhread_flag_deallocate_user_stack_on_exit will automatically be set in the thread flags.
 * @param user_stack_size         The size of the user stack for the uthread.
 * @param user_space              The virtual address space for the new uthread. If `NULL`, a new address space is created and ::futhread_flag_destroy_address_space_on_exit and ::futhread_flag_deallocate_address_space_on_exit are automatically set in the uthread flags.
 * @param flags                   Flags to assign to the uthread.
 * @param syscall_handler         An optional syscall handler for the uthread.
 * @param syscall_handler_context An optional context for the syscall handler for the uthread.
 *
 * @note The newly created uthread is suspended on creation. However, in order to start it, it must first be assigned to a thread manager (like the scheduler subsystem). Then, it can be resumed with fthread_resume() (using the uthread's core thread handle).
 *
 * @note All uthreads must start in kernel-space. They can switch to user-space later if necessary.
 *
 * @note The threads subsystem, uthreads subsystem, and/or thread manager may need to use part of the kernel stack before the initializer is called.
 *
 * @note The caller is granted a single reference to the new uthread.
 *
 * @warning This function should ONLY be called from a thread context, NOT an interrupt context.
 *
 * @retval ferr_ok                  The uthread was successfully allocated and initialized.
 * @retval ferr_already_in_progress The given thread was already a uthread.
 * @retval ferr_invalid_argument    One or more of: 1) @p thread was not a valid thread, 2) @p flags contained an invalid value.
 * @retval ferr_temporary_outage    One or more of: 1) there were insufficient resources to register a new uthread, 2) there was not enough memory to allocate a stack.
 */
FERRO_WUR ferr_t futhread_register(fthread_t* thread, void* user_stack_base, size_t user_stack_size, fpage_space_t* user_space, futhread_flags_t flags, futhread_syscall_handler_f syscall_handler, void* syscall_handler_context);

/**
 * Retrieves a pointer to the uthread that is currently executing on the current CPU.
 *
 * The returned pointer MAY be `NULL` if there is no active uthread on the current CPU.
 *
 * In an interrupt context, this will return the uthread that was executing when the interrupt occurred.
 *
 * @note This function DOES NOT grant a reference on the uthread. However, because this returns the *current* uthread, callers can rest assured that the uthread *is* valid.
 */
fthread_t* futhread_current(void);

/**
 * Jumps the given uthread into userspace at the given address.
 *
 * @param uthread The uthread to jump into userspace.
 * @param address The userspace address to jump to.
 *                This must be a valid userspace address within the given uthread's address space.
 *
 * @todo This function currently only works with the current uthread (i.e. futhread_current())
 *
 * @note When @p uthread is the current uthread, this function does not return.
 *
 * @retval ferr_ok The given uthread was successfully jumped into userspace at the given address.
 * @retval ferr_invalid_argument One or more of: 1) the given thread was invalid (e.g. not a uthread), 2) the given address was not a valid address within the uthread's user address space.
 */
FERRO_WUR ferr_t futhread_jump_user(fthread_t* uthread, void* address);

/**
 * Jumps the current uthread into userspace at the given address.
 *
 * @note This is a convenience wrapper around futhread_jump_user(). It also tells the compiler that this function should never return and thus allow it to make further optimizations/sanity checks.
 */
FERRO_NO_RETURN void futhread_jump_user_self(void* address);

/**
 * Initializes the uthreads subsystem.
 */
void futhread_init(void);

/**
 * Returns a pointer to the process to which this uthread belongs.
 *
 * @note UThreads can exist independently, without processes (though this is not common).
 *       Therefore, this function MAY return `NULL` even for valid uthreads.
 *
 * @note This does NOT grant a reference on the process. You must use `fproc_retain` for that.
 */
fproc_t* futhread_process(fthread_t* uthread);

/**
 * @param uthread                The uthread that will handle the signal.
 * @param signal                 The signal number to signal the target uthread with.
 * @param target_uthread         The uthread that is being signaled.
 * @param should_unblock_on_exit If `true`, the target uthread should be unblocked when the signal handler exits.
 *                               Note that this has nothing to do with blocking signals; this refers to the thread's
 *                               availability to be scheduled to run.
 * @param can_block              If `true`, the signal can be blocked; in this case, if the handling uthread is blocking signals,
 *                               this signal will simply be queued, even if it's configured as a preemptive signal.
 *                               If `false`, the signal cannot be blocked; in this case, if the handling uthread is blocking signals,
 *                               trying to queue the signal will fail.
 *                               Additionally, if the signal is successfully queued but the handling uthread is blocking the signal later
 *                               when it is going to be handled, the target uthread will be killed along with its process (if it belongs to one).
 */
FERRO_WUR ferr_t futhread_signal(fthread_t* uthread, uint64_t signal, fthread_t* target_uthread, bool should_unblock_on_exit, bool can_block);

FERRO_DECLARATIONS_END;

#endif // _FERRO_USERSPACE_THREADS_H_
