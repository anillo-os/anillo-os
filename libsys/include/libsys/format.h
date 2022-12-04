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

#ifndef _LIBSYS_FORMAT_H_
#define _LIBSYS_FORMAT_H_

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

#include <libsys/base.h>
#include <ferro/error.h>
#include <libsys/files.h>

LIBSYS_DECLARATIONS_BEGIN;

#define SYS_FORMAT_VARIANTS(_name, _format_index, _args_index_normal, _args_index_restricted, ...) \
	LIBSYS_WUR LIBSYS_PRINTF(_format_index, _args_index_normal) ferr_t sys_format_out_ ## _name(__VA_ARGS__, size_t* out_written_count, const char* format, ...); \
	LIBSYS_WUR LIBSYS_PRINTF(_format_index, _args_index_restricted) ferr_t sys_format_out_ ## _name ## _n(__VA_ARGS__, size_t* out_written_count, const char* format, size_t format_length, ...); \
	LIBSYS_WUR LIBSYS_PRINTF(_format_index, 0) ferr_t sys_format_out_ ## _name ## _v(__VA_ARGS__, size_t* out_written_count, const char* format, va_list arguments); \
	LIBSYS_WUR LIBSYS_PRINTF(_format_index, 0) ferr_t sys_format_out_ ## _name ## _nv(__VA_ARGS__, size_t* out_written_count, const char* format, size_t format_length, va_list arguments);

SYS_FORMAT_VARIANTS(buffer, 4, 5, 6, void* buffer, size_t buffer_size);
SYS_FORMAT_VARIANTS(file, 4, 5, 6, sys_file_t* file, uint64_t offset);

LIBSYS_DECLARATIONS_END;

#endif // _LIBSYS_FORMAT_H_
