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

#include "interface.h"

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
static void server_handler(void* context, eve_server_channel_t* server, sys_channel_t* channel) {
	spooky_interface_t* interface = context;

	if (spooky_interface_adopt(interface, channel, eve_loop_get_main()) != ferr_ok) {
		sys_release(channel);
	}
};

LIBSYS_STRUCT(sysman_foo) {
	uint64_t count;
};

static void foo_destructor(void* context) {
	sysman_foo_t* foo = context;
	sys_console_log_f("destroying foo with a count of %llu\n", foo->count);
	LIBSYS_WUR_IGNORE(sys_mempool_free(foo));
};

static void create_foo_impl(void* context, spooky_invocation_t* invocation) {
	ferr_t status = ferr_ok;
	spooky_proxy_interface_t* proxy_interface = context;
	sysman_foo_t* foo = NULL;
	spooky_proxy_t* proxy = NULL;

	status = sys_mempool_allocate(sizeof(*foo), NULL, (void*)&foo);
	if (status != ferr_ok) {
		goto out;
	}

	foo->count = 0;

	status = spooky_proxy_create(proxy_interface, foo, foo_destructor, &proxy);

out:
	LIBSPOOKY_WUR_IGNORE(spooky_invocation_set_proxy(invocation, 0, proxy));
	if (proxy) {
		spooky_release(proxy);
	}
	LIBSPOOKY_WUR_IGNORE(spooky_invocation_complete(invocation));
	spooky_release(invocation);
};

static void foo_add_impl(void* context, spooky_invocation_t* invocation) {
	sysman_foo_t* foo = context;
	uint64_t add;
	ferr_t status = ferr_ok;

	status = spooky_invocation_get_u64(invocation, 0, &add);
	if (status != ferr_ok) {
		goto out;
	}

	foo->count += add;

out:
	LIBSPOOKY_WUR_IGNORE(spooky_invocation_complete(invocation));
	spooky_release(invocation);
};

static void foo_count_impl(void* context, spooky_invocation_t* invocation) {
	sysman_foo_t* foo = context;

	LIBSPOOKY_WUR_IGNORE(spooky_invocation_set_u64(invocation, 0, foo->count));
	LIBSPOOKY_WUR_IGNORE(spooky_invocation_complete(invocation));
	spooky_release(invocation);
};
#endif

void main(void) {
#if 0
	start_process("/sys/netman/netman");
	start_process("/sys/usbman/usbman");

	eve_loop_run(eve_loop_get_main());
#endif
#if 1
	sys_server_channel_t* sys_server = NULL;
	eve_server_channel_t* server = NULL;
	spooky_interface_t* interface = NULL;
	spooky_proxy_interface_t* proxy_interface = NULL;

	sysman_test_interface_ensure();

	spooky_proxy_interface_entry_t proxy_interface_entries[] = {
		{
			.name = "add",
			.name_length = sizeof("add") - 1,
			.function = sysman_test_interface.foo_add_function,
			.implementation = foo_add_impl,
		},
		{
			.name = "count",
			.name_length = sizeof("count") - 1,
			.function = sysman_test_interface.foo_count_function,
			.implementation = foo_count_impl,
		},
	};

	sys_abort_status_log(spooky_proxy_interface_create(proxy_interface_entries, sizeof(proxy_interface_entries) / sizeof(*proxy_interface_entries), &proxy_interface));

	spooky_interface_entry_t interface_entries[] = {
		{
			.name = "create_foo",
			.name_length = sizeof("create_foo") - 1,
			.function = sysman_test_interface.create_foo_function,
			.implementation = create_foo_impl,
			.context = proxy_interface,
		},
	};

	sys_abort_status_log(spooky_interface_create(interface_entries, sizeof(interface_entries) / sizeof(*interface_entries), &interface));

	sys_abort_status_log(sys_server_channel_create(TEST_SERVER_NAME, sys_channel_realm_global, &sys_server));
	sys_abort_status_log(eve_server_channel_create(sys_server, interface, &server));
	sys_release(sys_server);
	eve_server_channel_set_handler(server, server_handler);
	sys_abort_status_log(eve_loop_add_item(eve_loop_get_main(), server));
	eve_release(server);

	start_process("/sys/sysman/tinysh");

	eve_loop_run(eve_loop_get_main());
#endif
};
