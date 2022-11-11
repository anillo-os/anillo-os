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
#include <stdatomic.h>

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

#if 0
#define THE_SIGNAL 8
#define PAGE_FAULT_SIGNAL 1

static void* good_addr = NULL;

static void signaling_thread(void* context, sys_thread_t* this_thread) {
	sys_thread_t* thread_to_signal = context;

	good_addr = &&jump_here_to_fix;

	while (true) {
		sys_console_log("going to signal.\n");
		//LIBSYS_WUR_IGNORE(sys_thread_signal(thread_to_signal, THE_SIGNAL));
		//LIBSYS_WUR_IGNORE(sys_thread_signal(sys_thread_current(), THE_SIGNAL));

		*(volatile uint8_t*)NULL = 255;
jump_here_to_fix:

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

static void page_fault_handler(void* context, sys_thread_signal_info_t* signal_info) {
	uint64_t alignment = 1ull << sys_config_read_minimum_thread_context_alignment_power();

	sys_console_log_f("page faulted\n");

	// to align this with a dynamic alignment, we have to over-allocate and then move over to an aligned address
	ferro_thread_context_t* target_thread_context = __builtin_alloca(sys_config_read_total_thread_context_size() + (alignment - 1));
	target_thread_context = (void*)(((uintptr_t)target_thread_context + (alignment - 1)) & ~(alignment - 1));
	void* instruction_pointer;

	sys_abort_status_log(sys_thread_execution_context(signal_info->thread, NULL, target_thread_context));

#if FERRO_ARCH == FERRO_ARCH_x86_64
	instruction_pointer = (void*)target_thread_context->rip;
#elif FERRO_ARCH == FERRO_ARCH_aarch64
	instruction_pointer = (void*)target_thread_context->pc;
#endif

	sys_console_log_f("fault occurred at %p\n", instruction_pointer);

	instruction_pointer = good_addr;

#if FERRO_ARCH == FERRO_ARCH_x86_64
	target_thread_context->rip = (uintptr_t)instruction_pointer;
#elif FERRO_ARCH == FERRO_ARCH_aarch64
	target_thread_context->pc = (uintptr_t)instruction_pointer;
#endif

	sys_abort_status_log(sys_thread_execution_context(signal_info->thread, target_thread_context, NULL));
};

__attribute__((aligned(4096)))
static char some_signal_stack[16ull * 1024];
#endif

#if 0
#define THREADS 2

atomic_uintmax_t counters[THREADS];

static void counting_thread(void* context, sys_thread_t* this_thread) {
	size_t id = (size_t)context;

	while (true) {
		++counters[id];
	}
};
#endif

void main(void) {
#if 1
	start_process("/sys/netman/netman");
	start_process("/sys/usbman/usbman");

	eve_loop_run(eve_loop_get_main());
#endif
#if 0
	sys_thread_signal_configuration_t config = {
		.flags = sys_thread_signal_configuration_flag_enabled | sys_thread_signal_configuration_flag_allow_redirection | sys_thread_signal_configuration_flag_preempt | sys_thread_signal_configuration_flag_mask_on_handle,
		.handler = signal_handler,
		.context = NULL,
	};
	sys_thread_signal_configuration_t page_fault_config = {
		.flags = sys_thread_signal_configuration_flag_enabled | sys_thread_signal_configuration_flag_allow_redirection | sys_thread_signal_configuration_flag_preempt | sys_thread_signal_configuration_flag_block_on_redirect | sys_thread_signal_configuration_flag_kill_if_unhandled,
		.handler = page_fault_handler,
		.context = NULL,
	};
	sys_thread_signal_stack_t stack = {
		.flags = 0,
		.base = some_signal_stack,
		.size = sizeof(some_signal_stack),
	};
	sys_thread_special_signal_mapping_t mapping = {
		.bus_error = 0,
		.page_fault = PAGE_FAULT_SIGNAL,
		.floating_point_exception = 0,
		.illegal_instruction = 0,
		.debug = 0,
	};

	sys_console_log_f("signal stack = (base = %p; top = %p)\n", some_signal_stack, &some_signal_stack[sizeof(some_signal_stack)]);

	sys_abort_status_log(sys_thread_signal_configure(THE_SIGNAL, &config, NULL));
	sys_abort_status_log(sys_thread_signal_configure(PAGE_FAULT_SIGNAL, &page_fault_config, NULL));

	sys_abort_status_log(sys_thread_signal_configure_special_mapping(sys_thread_current(), &mapping));

#if 0
	sys_abort_status_log(sys_thread_signal_stack_configure(sys_thread_current(), &stack, NULL));
#endif

	sys_abort_status_log(sys_thread_create(NULL, 2ull * 1024 * 1024, signaling_thread, sys_thread_current(), sys_thread_flag_resume, NULL));

	while (true) {
		sys_console_log("normal.\n");
		LIBSYS_WUR_IGNORE(sys_thread_suspend_timeout(sys_thread_current(), 1ull * 1000 * 1000 * 1000, sys_timeout_type_relative_ns_monotonic));
	}
#endif
#if 0
	sys_thread_t* threads[THREADS];

	// we create the threads and then resume them separately to avoid starting them at different times
	// (since thread creation can take a relatively long time)

	for (size_t id = 0; id < THREADS; ++id) {
		sys_abort_status_log(sys_thread_create(NULL, 512ull * 1024, counting_thread, (void*)id, 0, &threads[id]));
	}

	for (size_t id = 0; id < THREADS; ++id) {
		sys_abort_status(sys_thread_resume(threads[id]));
	}

	for (size_t iteration = 0; true; ++iteration) {
		atomic_uintmax_t values[THREADS];

		// we read the values first and the log them, because logging can be relatively slow
		// and we want to read the values as quickly as possible to avoid having too much difference between them

		for (size_t id = 0; id < THREADS; ++id) {
			values[id] = counters[id];
		}

		sys_console_log_f("Iteration %zu\n", iteration);

		for (size_t id = 0; id < THREADS; ++id) {
			sys_console_log_f("  Thread %zu = %lu\n", id, values[id]);
		}

		LIBSYS_WUR_IGNORE(sys_thread_suspend_timeout(sys_thread_current(), 1ull * 1000 * 1000 * 1000, sys_timeout_type_relative_ns_monotonic));
	}
#endif
};
