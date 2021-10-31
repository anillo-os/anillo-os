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

#include <libsys/console.private.h>
#include <libsimple/libsimple.h>
#include <libsys/streams.h>
#include <libsys/format.h>
#include <libsys/abort.h>

static sys_stream_handle_t console_handle = SYS_STREAM_HANDLE_INVALID;

ferr_t sys_console_init(void) {
	return sys_stream_open_special_handle(sys_stream_special_id_console_standard_output, &console_handle);
};

void sys_console_log(const char* string) {
	return sys_console_log_n(string, simple_strlen(string));
};

#define TEMPORARY_OUTAGE_RETRY_COUNT 5

void sys_console_log_n(const char* string, size_t string_length) {
	ferr_t status = ferr_ok;
	uint8_t retry_count = 0;
	size_t written_count = 0;

	while (written_count < string_length) {
		status = sys_stream_write_handle(console_handle, string_length - written_count, &string[written_count], &written_count);

		if (status == ferr_temporary_outage) {
			if (retry_count >= TEMPORARY_OUTAGE_RETRY_COUNT) {
				goto out;
			}
			++retry_count;
			continue;
		} else if (status != ferr_ok) {
			goto out;
		}
	}

out:
	return;
};

void sys_console_log_f(const char* format, ...) {
	va_list args;
	va_start(args, format);
	sys_console_log_fv(format, args);
	va_end(args);
};

void sys_console_log_fn(const char* format, size_t format_length, ...) {
	va_list args;
	va_start(args, format_length);
	sys_console_log_fnv(format, format_length, args);
	va_end(args);
};

void sys_console_log_fv(const char* format, va_list arguments) {
	return sys_console_log_fnv(format, simple_strlen(format), arguments);
};

void sys_console_log_fnv(const char* format, size_t format_length, va_list arguments) {
	sys_abort_status(sys_format_out_stream_handle_nv(console_handle, NULL, format, format_length, arguments));
};
