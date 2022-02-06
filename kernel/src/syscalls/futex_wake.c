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

ferr_t fsyscall_handler_futex_wake(uint64_t* address, uint64_t channel, uint64_t wakeup_count, uint64_t flags) {
	fproc_t* proc = fproc_current();
	futex_t* futex = NULL;
	ferr_t status = ferr_ok;

	if (futex_lookup(&proc->futex_table, (uintptr_t)address, channel, &futex) != ferr_ok) {
		status = ferr_temporary_outage;
		goto out;
	}

	fwaitq_wake_many(&futex->waitq, wakeup_count);

out:
	if (futex) {
		futex_release(futex);
	}
	return status;
};
