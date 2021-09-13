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
 * A packet buffer structure for the GDB stub subsystem, private components.
 */

#ifndef _FERRO_GDBSTUB_PACKET_BUFFER_PRIVATE_H_
#define _FERRO_GDBSTUB_PACKET_BUFFER_PRIVATE_H_

#include <stdint.h>

#include <ferro/base.h>
#include <ferro/gdbstub/packet-buffer.h>

FERRO_DECLARATIONS_BEGIN;

/**
 * @addtogroup GDB-Stub
 *
 * @{
 */

ferr_t fgdb_packet_buffer_serialize_u64(fgdb_packet_buffer_t* packet_buffer, uint64_t value, bool big_endian);
ferr_t fgdb_packet_buffer_serialize_u32(fgdb_packet_buffer_t* packet_buffer, uint32_t value, bool big_endian);
ferr_t fgdb_packet_buffer_serialize_u16(fgdb_packet_buffer_t* packet_buffer, uint16_t value, bool big_endian);
ferr_t fgdb_packet_buffer_serialize_u8(fgdb_packet_buffer_t* packet_buffer, uint8_t value, bool big_endian);

ferr_t fgdb_packet_buffer_deserialize_u64(fgdb_packet_buffer_t* packet_buffer, bool big_endian, uint64_t* out_value);
ferr_t fgdb_packet_buffer_deserialize_u32(fgdb_packet_buffer_t* packet_buffer, bool big_endian, uint32_t* out_value);
ferr_t fgdb_packet_buffer_deserialize_u16(fgdb_packet_buffer_t* packet_buffer, bool big_endian, uint16_t* out_value);
ferr_t fgdb_packet_buffer_deserialize_u8(fgdb_packet_buffer_t* packet_buffer, bool big_endian, uint8_t* out_value);

ferr_t fgdb_packet_buffer_serialize_data(fgdb_packet_buffer_t* packet_buffer, const uint8_t* data, size_t length);

/**
 * @}
 */

FERRO_DECLARATIONS_END;

#endif // _FERRO_GDBSTUB_PACKET_BUFFER_PRIVATE_H_
