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

#ifndef _LIBSYS_STREAMS_H_
#define _LIBSYS_STREAMS_H_

#include <stdint.h>
#include <stddef.h>

#include <libsys/base.h>
#include <ferro/error.h>
#include <libsys/objects.h>

LIBSYS_DECLARATIONS_BEGIN;

LIBSYS_OBJECT_CLASS(stream);

typedef uint64_t sys_stream_handle_t;

#define SYS_STREAM_HANDLE_INVALID UINT64_MAX

LIBSYS_ENUM(uint8_t, sys_stream_special_id) {
	sys_stream_special_id_console_standard_output,
};

LIBSYS_WUR ferr_t sys_stream_open_special(sys_stream_special_id_t special_id, sys_stream_t** out_stream);
LIBSYS_WUR ferr_t sys_stream_open_special_handle(sys_stream_special_id_t special_id, sys_stream_handle_t* out_stream_handle);

LIBSYS_WUR ferr_t sys_stream_close_handle(sys_stream_handle_t stream_handle);

LIBSYS_WUR ferr_t sys_stream_handle(sys_stream_t* stream, sys_stream_handle_t* out_stream_handle);

LIBSYS_WUR ferr_t sys_stream_read(sys_stream_t* stream, size_t buffer_size, void* out_buffer, size_t* out_read_count);
LIBSYS_WUR ferr_t sys_stream_read_handle(sys_stream_handle_t stream_handle, size_t buffer_size, void* out_buffer, size_t* out_read_count);

LIBSYS_WUR ferr_t sys_stream_write(sys_stream_t* stream, size_t buffer_length, const void* buffer, size_t* out_written_count);
LIBSYS_WUR ferr_t sys_stream_write_handle(sys_stream_handle_t stream_handle, size_t buffer_length, const void* buffer, size_t* out_written_count);

LIBSYS_DECLARATIONS_END;

#endif // _LIBSYS_STREAMS_H_
