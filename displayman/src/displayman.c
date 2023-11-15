/*
 * This file is part of Anillo OS
 * Copyright (C) 2023 Anillo OS Developers
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

FERRO_STRUCT(ferro_fb_info) {
	void* base;
	size_t width;
	size_t height;
	size_t scan_line_size;
	size_t pixel_bits;
	uint32_t red_mask;
	uint32_t green_mask;
	uint32_t blue_mask;
	uint32_t other_mask;
	size_t total_byte_size;
	uint8_t bytes_per_pixel;
};

static ferro_fb_info_t fb_info;
static sys_shared_memory_t* fb_memory = NULL;
static size_t fb_page_count = 0;

void main(void) {
	sys_channel_t* handoff_channel = NULL;
	sys_channel_message_t* outgoing_message = NULL;
	sys_channel_message_t* incoming_message = NULL;
	sys_channel_conversation_id_t convo_id = sys_channel_conversation_id_none;

	sys_abort_status_log(sys_proc_init_context_detach_object(0, &handoff_channel));

	sys_abort_status_log(sys_channel_message_create(0, &outgoing_message));
	sys_abort_status_log(sys_channel_send(handoff_channel, 0, outgoing_message, &convo_id));

	// sending the message consumes it
	outgoing_message = NULL;

	sys_abort_status_log(sys_channel_receive(handoff_channel, 0, &incoming_message));

	if (sys_channel_message_length(incoming_message) != sizeof(fb_info)) {
		sys_console_log_f("Invalid handoff reply size %lu\n", sys_channel_message_length(incoming_message));
		sys_abort();
	}

	simple_memcpy(&fb_info, sys_channel_message_data(incoming_message), sizeof(fb_info));

#if 0
	sys_console_log_f(
		"fb info:\n"
		"width=%lu; height=%lu;\n"
		"scan_line_size=%lu; pixel_bits=%lu\n"
		"red_mask=%u; green_mask=%u;\n"
		"blue_mask=%u; other_mask=%u;\n"
		"total_byte_size=%lu; bytes_per_pixel=%u\n",
		fb_info.width, fb_info.height,
		fb_info.scan_line_size, fb_info.pixel_bits,
		fb_info.red_mask, fb_info.green_mask,
		fb_info.blue_mask, fb_info.other_mask,
		fb_info.total_byte_size, fb_info.bytes_per_pixel
	);
#endif

	if (sys_channel_message_detach_shared_memory(incoming_message, 0, &fb_memory) == ferr_ok) {
		fb_page_count = sys_page_round_up_count(fb_info.total_byte_size);
		sys_abort_status_log(sys_shared_memory_map(fb_memory, fb_page_count, 0, &fb_info.base));

		// clear the screen
		simple_memset(fb_info.base, 0, fb_info.total_byte_size);
	} else {
		sys_console_log_f("Didn't get a framebuffer\n");
	}

	eve_loop_run(eve_loop_get_main());
};
