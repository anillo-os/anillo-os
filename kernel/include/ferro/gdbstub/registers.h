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
 * Register operations for the GDB stub subsystem.
 */

#ifndef _FERRO_GDBSTUB_REGISTERS_H_
#define _FERRO_GDBSTUB_REGISTERS_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include <ferro/base.h>
#include <ferro/gdbstub/packet-buffer.h>

FERRO_DECLARATIONS_BEGIN;

FERRO_STRUCT_FWD(fthread);

/**
 * @addtogroup GDB-Stub
 *
 * @{
 */

FERRO_OPTIONS(uint8_t, fgdb_registers_watchpoint_type) {
	fgdb_registers_watchpoint_type_read  = 1 << 0,
	fgdb_registers_watchpoint_type_write = 1 << 1,
};

ferr_t fgdb_registers_serialize_many(fgdb_packet_buffer_t* packet_buffer, fthread_t* thread);
ferr_t fgdb_registers_serialize_one(fgdb_packet_buffer_t* packet_buffer, fthread_t* thread, uintmax_t id);
ferr_t fgdb_registers_deserialize_many(fgdb_packet_buffer_t* packet_buffer, fthread_t* thread);
ferr_t fgdb_registers_deserialize_one(fgdb_packet_buffer_t* packet_buffer, fthread_t* thread, uintmax_t id);

void fgdb_registers_set_single_step(fthread_t* thread);
void fgdb_registers_clear_single_step(fthread_t* thread);

void fgdb_registers_skip_breakpoint(void);

ferr_t fgdb_registers_serialize_features(fgdb_packet_buffer_t* packet_buffer, const char* name, size_t name_length, size_t offset, size_t length);

ferr_t fgdb_registers_watchpoint_set(void* address, size_t size, fgdb_registers_watchpoint_type_t type);
ferr_t fgdb_registers_watchpoint_clear(void* address);

/**
 * @}
 */

FERRO_DECLARATIONS_END;

#endif // _FERRO_GDBSTUB_REGISTERS_H_
