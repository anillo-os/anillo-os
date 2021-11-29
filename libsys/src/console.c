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

sys_stream_handle_t console_handle = SYS_STREAM_HANDLE_INVALID;

ferr_t sys_console_init(void) {
	return sys_stream_open_special_handle(sys_stream_special_id_console_standard_output, &console_handle);
};

void sys_console_log(const char* string) {
	sys_console_log_c(string, NULL);
};

void sys_console_log_n(const char* string, size_t string_length) {
	sys_console_log_nc(string, string_length, NULL);
};

ferr_t sys_console_log_c(const char* string, size_t* out_written_count) {
	return sys_console_log_nc(string, simple_strlen(string), out_written_count);
};

#define TEMPORARY_OUTAGE_RETRY_COUNT 5

ferr_t sys_console_log_nc(const char* string, size_t string_length, size_t* out_written_count) {
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
	if (out_written_count) {
		*out_written_count = written_count;
	}
	return status;
};

void sys_console_log_f(const char* format, ...) {
	va_list args;
	va_start(args, format);
	sys_console_log_fvc(format, NULL, args);
	va_end(args);
};

void sys_console_log_fn(const char* format, size_t format_length, ...) {
	va_list args;
	va_start(args, format_length);
	sys_console_log_fnvc(format, format_length, NULL, args);
	va_end(args);
};

ferr_t sys_console_log_fc(const char* format, size_t* out_written_count, ...) {
	va_list args;
	va_start(args, out_written_count);
	ferr_t status = sys_console_log_fvc(format, out_written_count, args);
	va_end(args);
	return status;
};

ferr_t sys_console_log_fnc(const char* format, size_t format_length, size_t* out_written_count, ...) {
	va_list args;
	va_start(args, out_written_count);
	ferr_t status = sys_console_log_fnvc(format, format_length, out_written_count, args);
	va_end(args);
	return status;
};

void sys_console_log_fv(const char* format, va_list arguments) {
	sys_console_log_fvc(format, NULL, arguments);
};

void sys_console_log_fnv(const char* format, size_t format_length, va_list arguments) {
	sys_console_log_fnvc(format, format_length, NULL, arguments);
};

ferr_t sys_console_log_fvc(const char* format, size_t* out_written_count, va_list arguments) {
	return sys_console_log_fnvc(format, simple_strlen(format), out_written_count, arguments);
};

ferr_t sys_console_log_fnvc(const char* format, size_t format_length, size_t* out_written_count, va_list arguments) {
	return sys_format_out_stream_handle_nv(console_handle, out_written_count, format, format_length, arguments);
};
