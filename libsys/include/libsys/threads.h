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

#include <ferro/error.h>

LIBSYS_DECLARATIONS_BEGIN;

LIBSYS_OBJECT_CLASS(sys_thread);

typedef uint64_t sys_thread_id_t;
typedef void (*sys_thread_entry_f)(void* context, sys_thread_t* self);

#define SYS_THREAD_ID_INVALID UINT64_MAX

LIBSYS_ENUM(uint8_t, sys_thread_timeout_type) {
	sys_thread_timeout_type_relative_ns_monotonic,
	sys_thread_timeout_type_absolute_ns_monotonic,
};

LIBSYS_OPTIONS(uint64_t, sys_thread_flags) {
	/**
	 * Immediately start the thread running upon successful creation.
	 */
	sys_thread_flag_resume = 1ULL << 0,
};

/**
 * Creates a new thread with the given stack and entry point.
 *
 * @param stack      The base (i.e. lowest address) of the stack for the new thread.
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
LIBSYS_WUR ferr_t sys_thread_suspend_timeout(sys_thread_t* thread, uint64_t timeout, sys_thread_timeout_type_t timeout_type);
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

LIBSYS_DECLARATIONS_END;

#endif // _LIBSYS_THREADS_H_
