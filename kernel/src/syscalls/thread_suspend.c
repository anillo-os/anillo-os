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

#include <gen/ferro/userspace/syscall-handlers.h>
#include <ferro/core/threads.h>
#include <ferro/core/scheduler.h>
#include <ferro/userspace/threads.h>
#include <ferro/userspace/processes.h>
#include <ferro/core/scheduler.private.h>

ferr_t fsyscall_handler_thread_suspend(uint64_t thread_id, uint64_t timeout, fsyscall_timeout_type_t timeout_type) {
	ferr_t status = ferr_ok;
	fthread_t* thread = NULL;
	fthread_timeout_type_t thread_timeout_type;

	switch (timeout_type) {
		case fsyscall_timeout_type_none:
			// no timeout
			break;
		case fsyscall_timeout_type_ns_relative:
			thread_timeout_type = fthread_timeout_type_ns_relative;
			break;
		case fsyscall_timeout_type_ns_absolute_monotonic:
			thread_timeout_type = fthread_timeout_type_ns_absolute_monotonic;
			break;
		default:
			status = ferr_invalid_argument;
			goto out;
	}

	thread = fsched_find(thread_id, true);
	if (!thread) {
		status = ferr_no_such_resource;
		goto out;
	}

	if (thread == fthread_current()) {
		// no need to hold a reference to ourselves while we sleep
		fthread_release(thread);
	}

	// TODO: check whether the calling thread has the ability to suspend the given thread

	if (timeout_type == 0) {
		status = fthread_suspend(thread, false);
	} else {
		status = fthread_suspend_timeout(thread, false, timeout, thread_timeout_type);
	}

out:
	if (thread && thread != fthread_current()) {
		fthread_release(thread);
	}
	return status;
};

ferr_t fsyscall_handler_thread_yield(uint64_t thread_id) {
	ferr_t status = ferr_ok;
	fthread_t* thread = NULL;

	thread = fsched_find(thread_id, true);
	if (!thread) {
		status = ferr_no_such_resource;
		goto out;
	}

	flock_spin_intsafe_lock(&thread->lock);
	fsched_preempt_thread(thread);
	// `fsched_preempt_thread` drops the lock

out:
	if (thread) {
		fthread_release(thread);
	}
	return status;
};
