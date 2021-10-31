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

FERRO_DECLARATIONS_BEGIN;

FERRO_STRUCT_FWD(fproc);

FERRO_STRUCT(futhread_data_private) {
	futhread_data_t public;

	/**
	 * The process to which this thread belongs.
	 */
	fproc_t* process;
};

futhread_data_t* futhread_data_for_thread(fthread_t* thread);

// these are architecture-specific function we expect all architectures to implement

FERRO_NO_RETURN void futhread_jump_user_self_arch(fthread_t* uthread, futhread_data_t* udata, void* address);

void futhread_ending_interrupt_arch(fthread_t* uthread, futhread_data_t* udata);

void futhread_arch_init(void);

FERRO_DECLARATIONS_END;

#endif // _FERRO_USERSPACE_THREADS_PRIVATE_H_
