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

#ifndef _LIBSYS_CONSOLE_H_
#define _LIBSYS_CONSOLE_H_

#include <stddef.h>
#include <stdarg.h>

#include <libsys/base.h>
#include <ferro/error.h>

LIBSYS_DECLARATIONS_BEGIN;

void sys_console_log(const char* string);
void sys_console_log_n(const char* string, size_t string_length);

ferr_t sys_console_log_c(const char* string, size_t* out_written_count);
ferr_t sys_console_log_nc(const char* string, size_t string_length, size_t* out_written_count);

LIBSYS_PRINTF(1, 2) void sys_console_log_f(const char* format, ...);
LIBSYS_PRINTF(1, 3) void sys_console_log_fn(const char* format, size_t format_length, ...);

LIBSYS_PRINTF(1, 3) ferr_t sys_console_log_fc(const char* format, size_t* out_written_count, ...);
LIBSYS_PRINTF(1, 4) ferr_t sys_console_log_fnc(const char* format, size_t format_length, size_t* out_written_count, ...);

LIBSYS_PRINTF(1, 0) void sys_console_log_fv(const char* format, va_list arguments);
LIBSYS_PRINTF(1, 0) void sys_console_log_fnv(const char* format, size_t format_length, va_list arguments);

LIBSYS_PRINTF(1, 0) ferr_t sys_console_log_fvc(const char* format, size_t* out_written_count, va_list arguments);
LIBSYS_PRINTF(1, 0) ferr_t sys_console_log_fnvc(const char* format, size_t format_length, size_t* out_written_count, va_list arguments);

LIBSYS_DECLARATIONS_END;

#endif // _LIBSYS_CONSOLE_H_
