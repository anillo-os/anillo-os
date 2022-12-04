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

#include <vfs.server.h>
#include <libsys/general.private.h>

ferr_t vfsman_open_impl(void* _context, spooky_data_t* path, spooky_proxy_t** out_file, ferr_t* out_status) {
	// TODO
	sys_console_log_f("vfsman: Got request for %.*s\n", (int)spooky_data_length(path), (const char*)spooky_data_contents(path));
	return ferr_aborted;
};

static void start_sysman_work(void* context) {
	sys_file_t* sysman_file = NULL;
	sys_abort_status_log(sys_file_open("/sys/sysman/sysman", &sysman_file));
	sys_abort_status_log(sys_proc_create(sysman_file, NULL, 0, sys_proc_flag_resume, NULL));
	sys_release(sysman_file);
};

void start(void) asm("start");
void start(void) {
	eve_loop_t* main_loop = eve_loop_get_main();

	sys_abort_status_log(sys_init_core_full());
	sys_abort_status_log(sys_init_support());
	sys_abort_status_log(vfsman_serve(main_loop, NULL));
	sys_abort_status_log(eve_loop_enqueue(main_loop, start_sysman_work, NULL));
	eve_loop_run(main_loop);

	// we should never get here, but just in case
	sys_exit(0);
};
