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

#include <gen/ferro/userspace/syscall-handlers.h>
#include <ferro/userspace/threads.h>
#include <ferro/userspace/processes.h>
#include <ferro/core/panic.h>

static bool exit_thread_iterator(void* context, fproc_t* process, fthread_t* thread) {
	if (thread == fthread_current()) {
		// don't kill the current thread
		return true;
	}

	fpanic_status(fthread_kill(thread));

	return true;
};

ferr_t fsyscall_handler_exit(int32_t status) {
	// TODO: use `status` to indicate whether the process died peacefully or not

	// first kill the other threads in the process
	fproc_for_each_thread(fproc_current(), exit_thread_iterator, NULL);

	// now kill this thread
	fthread_kill_self();

	// unnecessary, but just for consistency
	return ferr_ok;
};
