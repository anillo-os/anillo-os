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

#include <ferro/userspace/syscalls.h>
#include <ferro/userspace/threads.h>
#include <ferro/core/panic.h>

extern uint64_t farch_syscall_handler_invoke(void* handler, fthread_saved_context_t* user_context);

void fsyscall_table_handler(void* context, fthread_t* uthread, fthread_saved_context_t* user_context) {
	const fsyscall_table_t* table = context;

	if (table->count < 1) {
		fpanic("Syscall table must have at least one entry");
	}

	if (user_context->rax == 0 || user_context->rax >= table->count) {
		user_context->rax = ((fsyscall_handler_lookup_error_f)table->handlers[0])(user_context->rax);
	} else {
		user_context->rax = farch_syscall_handler_invoke(table->handlers[user_context->rax], user_context);
	}
};
