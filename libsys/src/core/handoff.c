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

#include <libsys/handoff.private.h>
#include <libsys/threads.private.h>
#include <libsys/processes.private.h>
#include <libsys/mempool.private.h>

ferr_t sys_handoff_source(sys_handoff_context_t* context) {
	context->mempool_lock = &mempool_global_lock;
	context->mempool_main_instance = &mempool_main_instance;
	return ferr_ok;
};

ferr_t sys_handoff_destination(sys_handoff_context_t* context) {
	ferr_t status = ferr_ok;

	// thread initialization should be done in the loaded dylib (which is what's being initialized here)
	status = sys_thread_init();
	if (status != ferr_ok) {
		goto out;
	}

out:
	return status;
};
