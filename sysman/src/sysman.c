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
#include <libeve/libeve.h>

#define SYNC_LOG 1

static sys_mutex_t console_mutex = SYS_MUTEX_INIT;

__attribute__((format(printf, 1, 2)))
static void sysman_log_f(const char* format, ...) {
	va_list args;

#if SYNC_LOG
	sys_mutex_lock(&console_mutex);
#endif

	va_start(args, format);
	sys_console_log_fv(format, args);
	va_end(args);

#if SYNC_LOG
	sys_mutex_unlock(&console_mutex);
#endif
};

static void start_process(const char* filename) {
	sys_proc_t* proc = NULL;
	sys_file_t* file = NULL;

	sys_abort_status_log(sys_file_open(filename, &file));

	sysman_log_f("starting %s...\n", filename);
	sys_abort_status_log(sys_proc_create(file, NULL, 0, sys_proc_flag_resume | sys_proc_flag_detach, &proc));
	sysman_log_f("%s started with PID = %llu\n", filename, sys_proc_id(proc));

	sys_release(file);
	file = NULL;

	sys_release(proc);
	proc = NULL;
};

#if 1
#define THE_SIGNAL 8

static void signaling_thread(void* context, sys_thread_t* this_thread) {
	sys_thread_t* thread_to_signal = context;

	while (true) {
		sys_console_log("going to signal.\n");
		//LIBSYS_WUR_IGNORE(sys_thread_signal(thread_to_signal, THE_SIGNAL));
		LIBSYS_WUR_IGNORE(sys_thread_signal(sys_thread_current(), THE_SIGNAL));

		LIBSYS_WUR_IGNORE(sys_thread_suspend_timeout(sys_thread_current(), 5ull * 1000 * 1000 * 1000, sys_timeout_type_relative_ns_monotonic));
	}
};

static void signal_handler(void* context, sys_thread_signal_info_t* signal_info) {
	void* stack_pointer;
#if FERRO_ARCH == FERRO_ARCH_x86_64
	__asm__ volatile("mov %%rsp, %0" : "=r" (stack_pointer));
#elif FERRO_ARCH == FERRO_ARCH_aarch64
	__asm__ volatile("mov %0, sp" : "=r" (stack_pointer));
#endif
	sys_console_log_f("signal (sp = %p; target thread id = %llu)! waiting 10 seconds...\n", stack_pointer, sys_thread_id(signal_info->thread));

	for (size_t i = 0; i < 10; ++i) {
		sys_console_log_f("%zu\n", i);
		LIBSYS_WUR_IGNORE(sys_thread_suspend_timeout(sys_thread_current(), 1ull * 1000 * 1000 * 1000, sys_timeout_type_relative_ns_monotonic));
	}
};

__attribute__((aligned(4096)))
static char some_signal_stack[16ull * 1024];
#endif

void main(void) {
#if 0
	start_process("/sys/netman/netman");
	start_process("/sys/usbman/usbman");

	eve_loop_run(eve_loop_get_main());
#endif
#if 1
	sys_thread_signal_configuration_t config = {
		.flags = sys_thread_signal_configuration_flag_enabled | sys_thread_signal_configuration_flag_allow_redirection | sys_thread_signal_configuration_flag_preempt | sys_thread_signal_configuration_flag_mask_on_handle,
		.handler = signal_handler,
		.context = NULL,
	};
	sys_thread_signal_stack_t stack = {
		.flags = 0,
		.base = some_signal_stack,
		.size = sizeof(some_signal_stack),
	};

	sys_console_log_f("signal stack = (base = %p; top = %p)\n", some_signal_stack, &some_signal_stack[sizeof(some_signal_stack)]);

	sys_abort_status_log(sys_thread_signal_configure(THE_SIGNAL, &config, NULL));
#if 0
	sys_abort_status_log(sys_thread_signal_stack_configure(sys_thread_current(), &stack, NULL));
#endif

	sys_abort_status_log(sys_thread_create(NULL, 2ull * 1024 * 1024, signaling_thread, sys_thread_current(), sys_thread_flag_resume, NULL));

	while (true) {
		sys_console_log("normal.\n");
		LIBSYS_WUR_IGNORE(sys_thread_suspend_timeout(sys_thread_current(), 1ull * 1000 * 1000 * 1000, sys_timeout_type_relative_ns_monotonic));
	}
#endif
};
