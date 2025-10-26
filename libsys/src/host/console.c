/*
 * This file is part of Anillo OS
 * Copyright (C) 2024 Anillo OS Developers
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

#include <libsys/console.h>
#include <libsimple/libsimple.h>

#include <stdio.h>

void sys_console_log(const char* string) {
	return sys_console_log_n(string, simple_strlen(string));
};

void sys_console_log_n(const char* string, size_t string_length) {
	sys_console_log_nc(string, string_length, NULL);
};

ferr_t sys_console_log_c(const char* string, size_t* out_written_count) {
	return sys_console_log_nc(string, simple_strlen(string), out_written_count);
};

ferr_t sys_console_log_nc(const char* string, size_t string_length, size_t* out_written_count) {
	size_t written = fwrite(string, string_length, 1, stdout);
	if (out_written_count) {
		*out_written_count = written;
	}
	return ferr_ok;
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
	int result = vprintf(format, arguments);
	if (out_written_count && result >= 0) {
		*out_written_count = result;
	}
	return (result < 0) ? ferr_unknown : ferr_ok;
};

ferr_t sys_console_log_fnvc(const char* format, size_t format_length, size_t* out_written_count, va_list arguments) {
	return ferr_unsupported;
};

