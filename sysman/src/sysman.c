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

#include <libsys/libsys.h>

static void secondary_thread_entry(void* context, sys_thread_t* this_thread) {
	sys_console_log("secondary thread entering...\n");

	sys_console_log("secondary thread sleeping for 5 seconds...\n");
	sys_abort_status(sys_thread_suspend_timeout(this_thread, 5ULL * 1000000000ULL, sys_thread_timeout_type_relative_ns_monotonic));
	sys_console_log("secondary thread exiting...\n");
};

void main(void) {
	sys_thread_t* thread = NULL;
	void* stack = NULL;
	volatile bool foo = false;

	sys_console_log("*** sysman starting up... ***\n");

	sys_abort_status(sys_page_allocate(sys_config_read_minimum_stack_size() / sys_config_read_page_size(), 0, &stack));
	sys_console_log_f("allocated stack at %p\n", stack);

	sys_abort_status(sys_thread_create(stack, sys_config_read_minimum_stack_size(), secondary_thread_entry, NULL, sys_thread_flag_resume, &thread));
	sys_console_log("created and started secondary thread\n");

	sys_console_log("waiting for secondary thread to die...\n");
	sys_abort_status(sys_thread_wait(thread));
	sys_console_log("secondary thread died\n");

	sys_exit(0);
};
