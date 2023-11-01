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
#include <ferro/userspace/processes.private.h>
#include <ferro/userspace/threads.private.h>
#include <ferro/core/scheduler.h>
#include <ferro/userspace/syscalls.h>
#include <ferro/userspace/uio.h>

static void fproc_secondary_thread_init(void* entry) {
	futhread_jump_user_self(entry);
};

ferr_t fsyscall_handler_thread_create(void* stack, uint64_t stack_size, void const* entry, uint64_t* out_thread_id) {
	fproc_t* proc = fproc_current();
	fthread_t* thread = NULL;
	ferr_t status = ferr_ok;
	bool unmanage = false;
	futhread_data_private_t* private_data = NULL;

	if (fthread_new(fproc_secondary_thread_init, (void*)entry, NULL, FPAGE_LARGE_PAGE_SIZE, 0, &thread) != ferr_ok) {
		status = ferr_temporary_outage;
		goto out;
	}

	if (fsched_manage(thread) != ferr_ok) {
		status = ferr_temporary_outage;
		goto out;
	}
	unmanage = true;

	// register a userspace context onto the new thread
	if (futhread_register(thread, stack, stack_size, &proc->space, 0, fsyscall_table_handler, (void*)&fsyscall_table_standard) != ferr_ok) {
		status = ferr_temporary_outage;
		goto out;
	}

	// attach it to our process
	if (fproc_attach_thread(proc, thread) != ferr_ok) {
		status = ferr_temporary_outage;
		goto out;
	}

	status = ferro_uio_copy_out(&thread->id, sizeof(thread->id), (uintptr_t)out_thread_id);

out:
	if (status != ferr_ok) {
		if (unmanage) {
			// currently, the only way to make the scheduler unmanage a thread is to kill it
			FERRO_WUR_IGNORE(fthread_kill(thread));
		}
	}
	// we always release the thread, since the process retains it when we attach it
	if (thread) {
		fthread_release(thread);
	}
	return status;
};
