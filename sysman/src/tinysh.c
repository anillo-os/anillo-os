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

#include <libsys/libsys.h>
#include <libeve/libeve.h>
#include <libspooky/libspooky.h>

#include "interface.h"

static void work(void* context) {
	sys_channel_t* sys_channel = NULL;
	eve_channel_t* channel = NULL;
	spooky_invocation_t* invocation = NULL;
	spooky_proxy_t* foo = NULL;
	uint64_t foo_count = UINT64_MAX;

	sysman_test_interface_ensure();

	sys_abort_status_log(sys_channel_connect("org.anillo.sysman.test", sys_channel_realm_global, 0, &sys_channel));
	sys_abort_status_log(eve_channel_create(sys_channel, NULL, &channel));
	sys_release(sys_channel);
	sys_abort_status_log(eve_loop_add_item(eve_loop_get_current(), channel));
	eve_release(channel);

	sys_abort_status_log(spooky_invocation_create("create_foo", sizeof("create_foo") - 1, sysman_test_interface.create_foo_function, channel, &invocation));
	sys_abort_status_log(spooky_invocation_execute_sync(invocation));
	sys_abort_status_log(spooky_invocation_get_proxy(invocation, 0, true, &foo));
	spooky_release(invocation);

	// we no longer need the initial channel
	//
	// this will release the channel, closing it in the process
	sys_abort_status_log(eve_loop_remove_item(eve_loop_get_current(), channel));

	sys_abort_status_log(spooky_invocation_create_proxy("add", sizeof("add") - 1, sysman_test_interface.foo_add_function, foo, &invocation));
	sys_abort_status_log(spooky_invocation_set_u64(invocation, 0, 7));
	sys_abort_status_log(spooky_invocation_execute_sync(invocation));
	spooky_release(invocation);

	sys_abort_status_log(spooky_invocation_create_proxy("count", sizeof("count") - 1, sysman_test_interface.foo_count_function, foo, &invocation));
	sys_abort_status_log(spooky_invocation_execute_sync(invocation));
	sys_abort_status_log(spooky_invocation_get_u64(invocation, 0, &foo_count));
	spooky_release(invocation);

	sys_console_log_f("foo count after adding 7 = %llu\n", foo_count);

	foo_count = UINT64_MAX;

	sys_abort_status_log(spooky_invocation_create_proxy("add", sizeof("add") - 1, sysman_test_interface.foo_add_function, foo, &invocation));
	sys_abort_status_log(spooky_invocation_set_u64(invocation, 0, 38));
	sys_abort_status_log(spooky_invocation_execute_sync(invocation));
	spooky_release(invocation);

	sys_abort_status_log(spooky_invocation_create_proxy("count", sizeof("count") - 1, sysman_test_interface.foo_count_function, foo, &invocation));
	sys_abort_status_log(spooky_invocation_execute_sync(invocation));
	sys_abort_status_log(spooky_invocation_get_u64(invocation, 0, &foo_count));
	spooky_release(invocation);

	sys_console_log_f("foo count after adding 38 = %llu\n", foo_count);

	spooky_release(foo);
};

void main(void) {
	eve_loop_t* main_loop = eve_loop_get_main();

	LIBEVE_WUR_IGNORE(eve_loop_enqueue(main_loop, work, NULL));
	eve_loop_run(main_loop);
};
