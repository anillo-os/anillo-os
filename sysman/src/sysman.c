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
	volatile bool* foo = context;
	sys_console_log("*** secondary sysman thread starting up***\n");

	while (true) {
		if (*foo) {
			sys_console_log("foo was true!\n");
		} else {
			sys_console_log("foo was false!\n");
		}
		sys_console_log("secondary thread sleeping for 1 seconds\n");
		sys_abort_status(sys_thread_suspend_timeout(this_thread, 1000000000ULL * 1, sys_thread_timeout_type_relative_ns_monotonic));
	}
};

void main(void) {
	sys_thread_t* thread = NULL;
	void* stack = NULL;
	volatile bool foo = false;

	sys_console_log("*** sysman starting up... ***\n");

	sys_abort_status(sys_page_allocate(sys_config_read_minimum_stack_size() / sys_config_read_page_size(), 0, &stack));
	sys_console_log_f("allocated stack at %p\n", stack);

	sys_abort_status(sys_thread_create(stack, sys_config_read_minimum_stack_size(), secondary_thread_entry, (void*)&foo, sys_thread_flag_resume, &thread));
	sys_console_log("created and started thread\n");
	while (true) {
		for (size_t i = 0; i < (1ULL << 31); ++i) {
			foo = !foo;
		}
		sys_console_log("primary thread sleeping for 2 second\n");
		sys_abort_status(sys_thread_suspend_timeout(sys_thread_current(), 1000000000ULL * 2, sys_thread_timeout_type_relative_ns_monotonic));
	}

	sys_exit(0);
};
