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

#include "test.client.h"

static void work(void* context) {
	spooky_proxy_t* foo = NULL;
	uint64_t count = UINT64_MAX;

	sysman_test_create_foo(NULL, &foo);

	foo_add(foo, 7);
	foo_count(foo, &count);
	sys_console_log_f("foo count after adding 7 = %llu\n", count);

	count = UINT64_MAX;

	foo_add(foo, 38);
	foo_count(foo, &count);
	sys_console_log_f("foo count after adding 38 = %llu\n", count);

	spooky_release(foo);
};

void main(void) {
	eve_loop_t* main_loop = eve_loop_get_main();

	LIBEVE_WUR_IGNORE(eve_loop_enqueue(main_loop, work, NULL));
	eve_loop_run(main_loop);
};
