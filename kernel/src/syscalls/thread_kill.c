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

ferr_t fsyscall_handler_thread_kill(uint64_t thread_id) {
	ferr_t status = ferr_ok;
	fthread_t* thread = fsched_find(thread_id, true);

	if (!thread) {
		status = ferr_no_such_resource;
		goto out;
	}

	if (thread == fthread_current()) {
		// if it's the current thread, release the reference we gained on ourselves, and *then* kill ourselves.
		// (we can't possibly be fully released while we're running, the scheduler must be holding a reference to us)
		fthread_release(thread);
		fthread_kill_self();
		__builtin_unreachable();
	} else {
		status = fthread_kill(thread);
	}

out:
	if (thread) {
		fthread_release(thread);
	}
	return status;
};
