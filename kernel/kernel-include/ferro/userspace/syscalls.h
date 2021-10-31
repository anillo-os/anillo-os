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
 * System calls subsystem.
 */

#ifndef _FERRO_USERSPACE_SYSCALLS_H_
#define _FERRO_USERSPACE_SYSCALLS_H_

#include <stddef.h>
#include <stdint.h>

#include <ferro/base.h>
#include <ferro/error.h>

FERRO_DECLARATIONS_BEGIN;

FERRO_STRUCT_FWD(fthread);
FERRO_STRUCT_FWD(fthread_saved_context);

typedef ferr_t (*fsyscall_handler_lookup_error_f)(uint64_t syscall_number);

FERRO_STRUCT(fsyscall_table) {
	size_t count;

	/**
	 * Entry 0 is reserved for lookup errors and MUST be present.
	 * Entry 0 MUST be an ::fsyscall_handler_lookup_error_f.
	 */
	void* handlers[];
};

/**
 * A syscall table containing standard Ferro syscalls for the current platform.
 *
 * Note that syscalls are platform/architecture-specific, and therefore, this table will be different on each platform/architecture.
 * Userspace code making syscalls (which should ONLY be libsyscall doing it directly) is expected to know the right syscall numbers for the current platform/architecture.
 */
extern const fsyscall_table_t fsyscall_table_standard;

/**
 * A table-lookup handler for system calls.
 *
 * @param table        The table to use to lookup syscall handlers. This is the system call handler context argument passed to futhread_register().
 * @param uthread      The uthread that is making the system call.
 * @param user_context The saved userspace context of the given uthread.
 *
 * This handler can be provided to futhread_register() to handle system calls by forwarding them to handlers found in the table (passed as the syscall handler context).
 *
 * The ABI for syscalls is very similar to Linux's syscall ABI. As an example, this is the x86_64 Ferro syscall ABI:
 *   - A maximum of 6 arguments can be passed in registers rdi, rsi, rdx, r10, r8, and r9 (in order).
 *   - Return values are put into rax, but never rdx; syscalls are not allowed to return values larger than 64 bits.
 *   - Registers rcx and r11 are clobbered by the syscall instruction and are not preserved; all other registers not used for arguments or return values are preserved.
 *   - Floating point values are not allowed, only integers and memory addresses.
 *   - The syscall number is read from rax on entry and used to find the appropriate handler in the table.
 *
 * This handler takes care of putting the arguments in the right registers so they can be used as normal function arguments (regardless of architecture)
 * and also makes sure the return value is put in the right place.
 *
 * System call numbers start from 1, because system call handler 0 is reserved for lookup errors.
 *
 * System calls are expected to return ferr_t. This makes it easy to communicate errors between kernel-space and userspace, including syscall lookup errors.
 * This handler will assume all syscall handlers in the given table return ferr_t.
 */
void fsyscall_table_handler(void* table, fthread_t* uthread, fthread_saved_context_t* user_context);

FERRO_DECLARATIONS_END;

#endif // _FERRO_USERSPACE_SYSCALLS_H_
