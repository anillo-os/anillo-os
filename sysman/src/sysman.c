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
#include <libspooky/libspooky.h>

#include "test.server.h"

#define SYNC_LOG 1

#define TEST_SERVER_NAME "org.anillo.sysman.test"

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
LIBSYS_STRUCT(sysman_foo) {
	uint64_t count;
};

static void foo_destructor(void* context) {
	sysman_foo_t* foo = context;
	sys_console_log_f("destroying foo with a count of %llu\n", foo->count);
	LIBSYS_WUR_IGNORE(sys_mempool_free(foo));
};

static ferr_t foo_add_impl(void* _context, uint64_t value) {
	sysman_foo_t* foo = _context;
	foo->count += value;
	return ferr_ok;
};

static ferr_t foo_count_impl(void* _context, uint64_t* value) {
	sysman_foo_t* foo = _context;
	*value = foo->count;
	return ferr_ok;
};

ferr_t sysman_test_create_foo_impl(void* _context, spooky_proxy_t** out_foo) {
	sysman_foo_t* foo = NULL;

	sys_abort_status_log(sys_mempool_allocate(sizeof(*foo), NULL, (void*)&foo));

	foo->count = 0;

	foo_proxy_info_t proxy_info = {
		.context = foo,
		.destructor = foo_destructor,
		.add = foo_add_impl,
		.count = foo_count_impl,
	};

	sys_abort_status_log(foo_create_proxy(&proxy_info, out_foo));
	return ferr_ok;
};
#endif

void main(void) {
#if 0
	start_process("/sys/netman/netman");
	start_process("/sys/usbman/usbman");

	eve_loop_run(eve_loop_get_main());
#endif
#if 1
	sys_abort_status_log(sysman_test_serve(eve_loop_get_main(), NULL));

	start_process("/sys/sysman/tinysh");

	eve_loop_run(eve_loop_get_main());
#endif
};
