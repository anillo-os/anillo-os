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
#include <ferro/userspace/threads.private.h>

ferr_t fsyscall_handler_thread_set_tpidr(void* address) {
	fthread_t* thread = fthread_current();
	futhread_data_private_t* private_data = (void*)futhread_data_for_thread(thread);

	private_data->arch.tpidr_el0 = (uintptr_t)address;
	__asm__ volatile("msr tpidr_el0, %0" :: "r" (private_data->arch.tpidr_el0));

	return ferr_ok;
};
