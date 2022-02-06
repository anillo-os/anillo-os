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
#include <ferro/userspace/threads.private.h>

ferr_t fsyscall_handler_futex_associate(uint64_t* address, uint64_t channel, uint64_t event, uint64_t value) {
	fproc_t* proc = fproc_current();
	futex_t* futex = NULL;
	ferr_t status = ferr_ok;

	if (event > 0) {
		status = ferr_invalid_argument;
		goto out;
	}

	if (futex_lookup(&proc->futex_table, (uintptr_t)address, channel, &futex) != ferr_ok) {
		status = ferr_temporary_outage;
		goto out;
	}

	if (event == 0) {
		fthread_t* thread = fthread_current();
		futhread_data_private_t* private_data = (futhread_data_private_t*)futhread_data_for_thread(thread);

		// transfer ownership of the futex reference to the uthread
		private_data->uthread_death_futex = futex;
		futex = NULL;

		private_data->uthread_death_futex_value = value;
	}

out:
	if (futex) {
		futex_release(futex);
	}
	return status;
};
