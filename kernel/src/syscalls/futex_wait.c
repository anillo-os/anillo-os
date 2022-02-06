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
#include <ferro/userspace/processes.h>
#include <ferro/core/waitq.private.h>
#include <ferro/core/threads.private.h>

ferr_t fsyscall_handler_futex_wait(uint64_t* address, uint64_t channel, uint64_t expected_value, uint64_t timeout, uint8_t timeout_type, uint64_t flags) {
	fproc_t* proc = fproc_current();
	futex_t* futex = NULL;
	ferr_t status = ferr_ok;
	fthread_timeout_type_t thread_timeout_type;

	switch (timeout_type) {
		case 0:
			// no timeout
			break;
		case 1:
			thread_timeout_type = fthread_timeout_type_ns_relative;
			break;
		case 2:
			thread_timeout_type = fthread_timeout_type_ns_absolute_monotonic;
			break;
		default:
			status = ferr_invalid_argument;
			goto out;
	}

	if (futex_lookup(&proc->futex_table, (uintptr_t)address, channel, &futex) != ferr_ok) {
		status = ferr_temporary_outage;
		goto out;
	}

	fwaitq_lock(&futex->waitq);

	// check the value while holding the waitq lock
	//
	// this way, we're guaranteed to synchronize with any other wakeups on the same futex;
	// either they'll see us as a waiter, or we'll see their updated value.
	if (__atomic_load_n(address, __ATOMIC_RELAXED) != expected_value) {
		status = ferr_should_restart;
		fwaitq_unlock(&futex->waitq);
		goto out;
	}

	if (timeout_type == 0) {
		status = fthread_wait_locked(fthread_current(), &futex->waitq);
	} else {
		status = fthread_wait_timeout_locked(fthread_current(), &futex->waitq, timeout, thread_timeout_type);
	}

	if (status != ferr_ok) {
		fwaitq_unlock(&futex->waitq);
	}

out:
	if (futex) {
		futex_release(futex);
	}
	return status;
};
