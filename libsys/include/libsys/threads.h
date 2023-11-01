/*
 * This file is part of Anillo OS
 * Copyright (C) 2022 Anillo OS Developers
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

#ifndef _LIBSYS_THREADS_H_
#define _LIBSYS_THREADS_H_

#include <stdint.h>
#include <stddef.h>

#include <libsys/base.h>
#include <libsys/objects.h>
#include <libsys/timeout.h>

#include <ferro/api.h>
#include <ferro/error.h>

LIBSYS_DECLARATIONS_BEGIN;

LIBSYS_OBJECT_CLASS(thread);

LIBSYS_STRUCT_FWD(sys_thread_signal_info);

typedef uint64_t sys_thread_id_t;
typedef void (*sys_thread_entry_f)(void* context, sys_thread_t* self);
typedef void (*sys_thread_signal_handler_f)(void* context, sys_thread_signal_info_t* signal_info);

#define SYS_THREAD_ID_INVALID UINT64_MAX

LIBSYS_OPTIONS(uint64_t, sys_thread_flags) {
	/**
	 * Immediately start the thread running upon successful creation.
	 */
	sys_thread_flag_resume = 1ULL << 0,
};

LIBSYS_OPTIONS(uint64_t, sys_thread_signal_configuration_flags) {
	sys_thread_signal_configuration_flag_enabled = 1 << 0,
	sys_thread_signal_configuration_flag_coalesce = 1 << 1,
	sys_thread_signal_configuration_flag_allow_redirection = 1 << 2,
	sys_thread_signal_configuration_flag_preempt = 1 << 3,
	sys_thread_signal_configuration_flag_block_on_redirect = 1 << 4,
	sys_thread_signal_configuration_flag_mask_on_handle = 1 << 5,
	sys_thread_signal_configuration_flag_kill_if_unhandled = 1 << 6,
};

LIBSYS_STRUCT(sys_thread_signal_configuration) {
	sys_thread_signal_configuration_flags_t flags;
	sys_thread_signal_handler_f handler;
	void* context;
};

LIBSYS_OPTIONS(uint64_t, sys_thread_signal_stack_flags) {
	sys_thread_signal_stack_flag_clear_on_use = 1 << 0,
};

LIBSYS_STRUCT(sys_thread_signal_stack) {
	sys_thread_signal_stack_flags_t flags;
	void* base;
	size_t size;
};

LIBSYS_OPTIONS(uint64_t, sys_thread_signal_info_flags) {
	sys_thread_signal_info_flag_blocked = 1 << 0,
};

LIBSYS_STRUCT(sys_thread_signal_info) {
	sys_thread_signal_info_flags_t flags;
	uint64_t signal_number;
	sys_thread_t* thread;
	ferro_thread_context_t* handling_thread_context;
	uint64_t data;
	uint64_t mask;
};

LIBSYS_STRUCT(sys_thread_special_signal_mapping) {
	uint64_t bus_error;
	uint64_t page_fault;
	uint64_t floating_point_exception;
	uint64_t illegal_instruction;
	uint64_t debug;
	uint64_t division_by_zero;
};

/**
 * Creates a new thread with the given stack and entry point.
 *
 * @param stack      The base (i.e. lowest address) of the stack for the new thread.
 *                   If this is `NULL`, a stack with a size of @p stack_size is automatically allocated
 *                   by this function and freed upon thread death.
 * @param stack_size The size of the thread's stack, in bytes.
 * @param entry      The function to start the thread with.
 * @param context    An optional context argument for the thread entry function.
 * @param flags      An optional set of flags used to modify the new thread. See ::sys_thread_flags.
 * @param out_thread An optional pointer into which a reference to the thread object for the new thread will be written.
 *                   If this is provided, the caller is granted one reference on the new thread object.
 *                   This MUST be non-null if ::sys_thread_flag_resume is not set in @p flags.
 *
 * @note Threads are suspended upon creation by default. They must be manually started with sys_thread_resume().
 *       Alternatively, threads can be started upon creation using the ::sys_thread_flag_resume flag.
 *
 * @note The stack is used by this library to set up the the context for the new thread. Therefore, there is a minimum stack
 *       size that's required. See sys_config_read() and ::sys_config_key_minimum_stack_size (or, alternatively, sys_config_read_minimum_stack_size()).
 *
 * @retval ferr_success          The thread has been successfully created. If @p out_thread is non-null, the caller has been granted one reference on the new thread object.
 * @retval ferr_invalid_argument One or more of:
 *                                 1) @p stack_size was too small (which implicitly means the stack was too small),
 *                                 2) @p entry was not in executable memory,
 *                                 3) @p flags contained one or more invalid (or incompatible) flags,
 *                                 4) @p out_thread was `NULL` but @p flags did NOT contain ::sys_thread_flag_resume.
 * @retval ferr_temporary_outage The system did not have enough resources to create a new thread.
 * @retval ferr_forbidden        The caller did not have the necessary permissions to create a new thread.
 */
LIBSYS_WUR ferr_t sys_thread_create(void* stack, size_t stack_size, sys_thread_entry_f entry, void* context, sys_thread_flags_t flags, sys_thread_t** out_thread);
LIBSYS_WUR ferr_t sys_thread_resume(sys_thread_t* thread);
LIBSYS_WUR ferr_t sys_thread_suspend(sys_thread_t* thread);
LIBSYS_WUR ferr_t sys_thread_suspend_timeout(sys_thread_t* thread, uint64_t timeout, sys_timeout_type_t timeout_type);
LIBSYS_WUR ferr_t sys_thread_yield(sys_thread_t* thread);
sys_thread_t* sys_thread_current(void);

sys_thread_id_t sys_thread_id(sys_thread_t* thread);

/**
 * Waits for the given thread to exit.
 *
 * @param thread The thread to wait for.
 *
 * @retval ferr_ok               The thread has exited.
 * @retval ferr_invalid_argument The given thread was not a valid thread to wait for (e.g. it was the current thread).
 * @retval ferr_forbidden        Waiting for the given thread was not allowed.
 */
LIBSYS_WUR ferr_t sys_thread_wait(sys_thread_t* thread);

LIBSYS_WUR ferr_t sys_thread_signal(sys_thread_t* thread, uint64_t signal);
LIBSYS_WUR ferr_t sys_thread_signal_configure(uint64_t signal, const sys_thread_signal_configuration_t* new_configuration, sys_thread_signal_configuration_t* out_old_configuration);
LIBSYS_WUR ferr_t sys_thread_signal_stack_configure(sys_thread_t* thread, const sys_thread_signal_stack_t* new_stack, sys_thread_signal_stack_t* out_old_stack);

LIBSYS_WUR ferr_t sys_thread_signal_configure_special_mapping(sys_thread_t* thread, const sys_thread_special_signal_mapping_t* mapping);

/**
 * Prevents the given thread from:
 * 1. Handling any signals
 * 2. Having another thread handle its redirected signals
 *
 * Any signals that arrive while signals are blocked are queued for handling
 * after signals are unblocked, except for unblockable signals (e.g. page faults).
 *
 * If an unblockable signal arrives while the thread has signals blocked:
 * 1. If the signal's target thread is @p thread, the thread's process is killed.
 * 2. If no other thread is able to handle the redirected signal, the target thread's process is killed.
 *
 * @note This is only guaranteed to block signals immediately when @p thread
 *       is the current thread.
 */
void sys_thread_block_signals(sys_thread_t* thread);
void sys_thread_unblock_signals(sys_thread_t* thread);

LIBSYS_WUR ferr_t sys_thread_block(sys_thread_t* thread);
LIBSYS_WUR ferr_t sys_thread_unblock(sys_thread_t* thread);

/**
 * Can only be called on either the current thread or a blocked thread.
 */
LIBSYS_WUR ferr_t sys_thread_execution_context(sys_thread_t* thread, const ferro_thread_context_t* new_context, ferro_thread_context_t* out_old_context);

LIBSYS_DECLARATIONS_END;

#endif // _LIBSYS_THREADS_H_
