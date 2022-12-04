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

#include <libsys/format.private.h>
#include <libsimple/general.h>

SYS_FORMAT_VARIANT_WRAPPER(file, { sys_file_t* file; uint64_t offset; }, SYS_FORMAT_CONTEXT_INIT(file, offset), sys_file_t* file, uint64_t offset) {
	SYS_FORMAT_WRITE_HEADER(file);

	ferr_t status = sys_file_write(context->file, context->offset, buffer_length, buffer, out_written_count);

	if (status == ferr_ok) {
		context->offset += *out_written_count;
	}

	return status;
};
