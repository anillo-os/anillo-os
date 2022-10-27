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

static void message_handler(void* context, eve_channel_t* channel, sys_channel_message_t* message) {
	sys_shared_memory_t* shared_memory = NULL;
	uint64_t* ptr1 = NULL;
	uint64_t* ptr3 = NULL;

	sys_abort_status_log(sys_channel_message_detach_shared_memory(message, 0, &shared_memory));
	sys_release(message);

	sys_abort_status_log(sys_shared_memory_map(shared_memory, 1, 0, (void*)&ptr1));
	sys_abort_status_log(sys_shared_memory_map(shared_memory, 1, 2, (void*)&ptr3));
	sys_release(shared_memory);

	sys_console_log_f("tinysh: mapped to %p and %p\n", ptr1, ptr3);

	while (true) {
		sys_console_log_f("current value = %llu, ~, %llu\n", *ptr1, *ptr3);
		// 1s
		LIBSYS_WUR_IGNORE(sys_thread_suspend_timeout(sys_thread_current(), 1ull * 1000 * 1000 * 1000, sys_timeout_type_relative_ns_monotonic));
	}
};

static void peer_close_handler(void* context, eve_channel_t* channel) {
	sys_console_log_f("server closed their end\n");
	sys_abort_status_log(eve_loop_remove_item(eve_loop_get_current(), channel));
};

static void message_send_error_handler(void* context, eve_channel_t* channel, sys_channel_message_t* message, ferr_t error) {
	sys_console_log_f("message send error = %d\n", error);
	sys_release(message);
};

void main(void) {
	eve_loop_t* main_loop = eve_loop_get_main();
	sys_channel_t* sys_channel = NULL;
	eve_channel_t* channel = NULL;

	sys_abort_status_log(sys_channel_connect("org.anillo.sysman.test", sys_channel_realm_global, 0, &sys_channel));
	sys_abort_status_log(eve_channel_create(sys_channel, NULL, &channel));
	sys_release(sys_channel);
	eve_channel_set_message_handler(channel, message_handler);
	eve_channel_set_peer_close_handler(channel, peer_close_handler);
	eve_channel_set_message_send_error_handler(channel, message_send_error_handler);
	sys_abort_status_log(eve_loop_add_item(main_loop, channel));
	eve_release(channel);

	eve_loop_run(main_loop);
};
