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
#include <ferro/userspace/process-registry.h>

ferr_t fsyscall_handler_process_suspend(uint64_t process_id) {
	ferr_t status = ferr_ok;
	fproc_t* proc = NULL;

	status = fprocreg_lookup(process_id, true, &proc);
	if (status != ferr_ok) {
		status = ferr_no_such_resource;
		goto out;
	}

	if (proc == fproc_current()) {
		// no need to hold a reference to ourselves while we sleep
		fproc_release(proc);
	}

	status = fproc_suspend(proc);

out:
	if (proc && proc != fproc_current()) {
		fproc_release(proc);
	}
	return status;
};
