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

#ifndef _LIBSYS_FORMAT_PRIVATE_H_
#define _LIBSYS_FORMAT_PRIVATE_H_

#include <libsys/format.h>

SYS_FORMAT_VARIANTS(console, 3, 4, 5, int ignored);

/**
 * @retval ferr_ok At least some data was successfully written.
 * @retval ferr_temporary_outage No data was able to written.
 *
 * @note If this function returns ::ferr_ok but `0` is written into @p out_written_count, it will be treated as if ::ferr_temporary_outage had been returned.
 */
typedef ferr_t (*sys_format_write_f)(void* context, const void* buffer, size_t buffer_length, size_t* out_written_count);

LIBSYS_WUR ferr_t __sys_format_out(void* context, sys_format_write_f write, size_t* out_written_count, const char* format, size_t format_length, va_list args);

#define SYS_FORMAT_VARIANT_WRAPPER(_name, _context_decl, _context_init, ...) \
	LIBSYS_STRUCT(sys_format_out_ ## _name ## _context) _context_decl; \
	static ferr_t sys_format_out_ ## _name ## _write(void* context, const void* buffer, size_t buffer_length, size_t* out_written_count); \
	ferr_t sys_format_out_ ## _name(__VA_ARGS__, size_t* out_written_count, const char* format, ...) { \
		sys_format_out_ ## _name ## _context_t context = _context_init; \
		va_list args; \
		va_start(args, format); \
		ferr_t status = __sys_format_out(&context, sys_format_out_ ## _name ## _write, out_written_count, format, simple_strlen(format), args); \
		va_end(args); \
		return status; \
	}; \
	ferr_t sys_format_out_ ## _name ## _n(__VA_ARGS__, size_t* out_written_count, const char* format, size_t format_length, ...) { \
		sys_format_out_ ## _name ## _context_t context = _context_init; \
		va_list args; \
		va_start(args, format_length); \
		ferr_t status = __sys_format_out(&context, sys_format_out_ ## _name ## _write, out_written_count, format, format_length, args); \
		va_end(args); \
		return status; \
	}; \
	ferr_t sys_format_out_ ## _name ## _v(__VA_ARGS__, size_t* out_written_count, const char* format, va_list arguments) { \
		sys_format_out_ ## _name ## _context_t context = _context_init; \
		return __sys_format_out(&context, sys_format_out_ ## _name ## _write, out_written_count, format, simple_strlen(format), arguments); \
	}; \
	ferr_t sys_format_out_ ## _name ## _nv(__VA_ARGS__, size_t* out_written_count, const char* format, size_t format_length, va_list arguments) { \
		sys_format_out_ ## _name ## _context_t context = _context_init; \
		return __sys_format_out(&context, sys_format_out_ ## _name ## _write, out_written_count, format, format_length, arguments); \
	}; \
	static ferr_t sys_format_out_ ## _name ## _write(void* xcontext, const void* buffer, size_t buffer_length, size_t* out_written_count)

#define SYS_FORMAT_WRITE_HEADER(_name) \
	sys_format_out_ ## _name ## _context_t* context = xcontext;

#define SYS_FORMAT_CONTEXT_INIT(...) { __VA_ARGS__ }

#endif // _LIBSYS_FORMAT_PRIVATE_H_
