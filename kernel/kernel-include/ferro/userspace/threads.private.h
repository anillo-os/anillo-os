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
 * Userspace Threads subsystem, private components.
 */

#ifndef _FERRO_USERSPACE_THREADS_PRIVATE_H_
#define _FERRO_USERSPACE_THREADS_PRIVATE_H_

#include <ferro/userspace/threads.h>
#include <ferro/core/waitq.h>

// include the arch-dependent before-header
#if FERRO_ARCH == FERRO_ARCH_x86_64
	#include <ferro/userspace/x86_64/threads.private.before.h>
#elif FERRO_ARCH == FERRO_ARCH_aarch64
	#include <ferro/userspace/aarch64/threads.private.before.h>
#else
	#error Unrecognized/unsupported CPU architecture! (see <ferro/userspace/threads.private.h>)
#endif

#include <gen/ferro/userspace/syscall-handlers.h>
#include <ferro/core/ghmap.h>
#include <libsimple/ring.h>

FERRO_DECLARATIONS_BEGIN;

FERRO_STRUCT_FWD(fproc);
FERRO_STRUCT_FWD(futex);

FERRO_STRUCT(futhread_signal_handler) {
	uint64_t signal;
	fsyscall_signal_configuration_t configuration;
};

FERRO_STRUCT(futhread_pending_signal) {
	futhread_pending_signal_t** prev;
	// the next signal in the chain is lower priority than this one and/or was queued later than this one
	futhread_pending_signal_t* next;
	fthread_t* target_uthread;
	fsyscall_signal_configuration_t configuration;
	uint64_t signal;
	// if true, this signal blocked the target uthread and is responsible for unblocking it when the signal handler returns.
	bool was_blocked;
	bool exited;
	bool can_block;
};

FERRO_STRUCT(futhread_data_private) {
	futhread_data_t public;

	fthread_t* thread;

	/**
	 * The process to which this thread belongs.
	 */
	fproc_t* process;

	/**
	 * A link to the previous uthread in this uthread's process.
	 *
	 * This is ONLY to be accessed by this uthread's process (it is protected by that process's uthread list lock).
	 */
	futhread_data_private_t** prev;

	/**
	 * A link to the next uthread in this uthread's process.
	 *
	 * This is ONLY to be accessed by this uthread's process (it is protected by that process's uthread list lock).
	 */
	futhread_data_private_t* next;

	/**
	 * A waiter for this uthread's death; owned by this uthread's process.
	 */
	fwaitq_waiter_t uthread_death_waiter;

	/**
	 * A waiter for this uthread's destruction; owned by this uthread's process.
	 */
	fwaitq_waiter_t uthread_destroy_waiter;

	futex_t* uthread_death_futex;
	uint64_t uthread_death_futex_value;

	simple_ghmap_t signal_handler_table;
	futhread_pending_signal_t* pending_signal;
	futhread_pending_signal_t* last_pending_signal;
	futhread_pending_signal_t* current_signal;
	fsyscall_signal_mapping_t signal_mapping;
	fsyscall_signal_stack_t signal_stack;
	uint64_t signal_mask;
	flock_mutex_t signals_mutex;

	bool use_fake_interrupt_return;

	futhread_data_private_arch_t arch;

	void* faulted_memory_address;
};

futhread_data_t* futhread_data_for_thread(fthread_t* thread);

ferr_t futhread_handle_signals(fthread_t* uthread, bool locked);

// these are architecture-specific function we expect all architectures to implement

FERRO_NO_RETURN void futhread_jump_user_self_arch(fthread_t* uthread, futhread_data_t* udata, void* address);

void futhread_ending_interrupt_arch(fthread_t* uthread, futhread_data_t* udata);

void futhread_arch_init(void);
void futhread_arch_ensure_ready_cpu(void);
void futhread_arch_init_private_data(futhread_data_private_t* data);

FERRO_DECLARATIONS_END;

#endif // _FERRO_USERSPACE_THREADS_PRIVATE_H_
