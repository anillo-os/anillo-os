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

/**
 * @file
 *
 * A packet buffer structure for the GDB stub subsystem.
 */

#ifndef _FERRO_GDBSTUB_PACKET_BUFFER_H_
#define _FERRO_GDBSTUB_PACKET_BUFFER_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include <ferro/base.h>
#include <ferro/error.h>

FERRO_DECLARATIONS_BEGIN;

/**
 * @addtogroup GDB-Stub
 *
 * @{
 */

FERRO_STRUCT(fgdb_packet_buffer) {
	bool mempooled;
	uint8_t* buffer;
	size_t size;
	size_t length;
	size_t offset;
};

ferr_t fgdb_packet_buffer_init(fgdb_packet_buffer_t* packet_buffer, uint8_t* static_buffer, size_t static_buffer_size);
void fgdb_packet_buffer_destroy(fgdb_packet_buffer_t* packet_buffer);
ferr_t fgdb_packet_buffer_grow(fgdb_packet_buffer_t* packet_buffer);
ferr_t fgdb_packet_buffer_append(fgdb_packet_buffer_t* packet_buffer, const uint8_t* data, size_t length);

/**
 * @}
 */

FERRO_DECLARATIONS_END;

#endif // _FERRO_GDBSTUB_PACKET_BUFFER_H_
