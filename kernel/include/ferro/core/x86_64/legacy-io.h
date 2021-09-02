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
 * x86_64 legacy I/O subsystem.
 */

#ifndef _FERRO_CORE_X86_64_LEGACY_IO_H_
#define _FERRO_CORE_X86_64_LEGACY_IO_H_

#include <stdint.h>

#include <ferro/base.h>

FERRO_DECLARATIONS_BEGIN;

/**
 * @addtogroup Legacy-IO
 *
 * The x86_64 legacy IO subsystem.
 *
 * @{
 */

#define FARCH_LIO_READ_DEFINITION(ferro_suffix, type, instruction_suffix) \
	FERRO_ALWAYS_INLINE type farch_lio_read_ ## ferro_suffix(uint16_t port) { \
		type value; \
		__asm__ volatile("in" instruction_suffix " %1, %0" : "=a" (value) : "Nd" (port)); \
		return value; \
	};

FARCH_LIO_READ_DEFINITION( u8,  uint8_t, "b");
FARCH_LIO_READ_DEFINITION(u16, uint16_t, "w");
FARCH_LIO_READ_DEFINITION(u32, uint32_t, "l");

#undef FARCH_LIO_IN_DEFINITION

#define FARCH_LIO_WRITE_DEFINITION(ferro_suffix, type, instruction_suffix) \
	FERRO_ALWAYS_INLINE void farch_lio_write_ ## ferro_suffix(uint16_t port, type value) { \
		__asm__ volatile("out" instruction_suffix " %0, %1" :: "a" (value), "Nd" (port)); \
	};

FARCH_LIO_WRITE_DEFINITION( u8,  uint8_t, "b");
FARCH_LIO_WRITE_DEFINITION(u16, uint16_t, "w");
FARCH_LIO_WRITE_DEFINITION(u32, uint32_t, "l");

#undef FARCH_LIO_WRITE_DEFINITION

/**
 * Waits the necessary amount of time to ensure a port read or write has been seen by the hardware.
 */
FERRO_ALWAYS_INLINE void farch_lio_wait(void) {
	__asm__ volatile("outb %0, $0x80" :: "a" (0));
};

/**
 * A list of well-known legacy IO ports.
 *
 * @note This is not by any means an exhaustive list.
 */
FERRO_ENUM(uint16_t, farch_lio_port) {
	farch_lio_port_pic_primary_command   = 0x20,
	farch_lio_port_pic_primary_data      = 0x21,

	farch_lio_port_pic_secondary_command = 0xa0,
	farch_lio_port_pic_secondary_data    = 0xa1,

	farch_lio_port_pit_data_channel_0    = 0x40,
	farch_lio_port_pit_data_channel_1    = 0x41,
	farch_lio_port_pit_data_channel_2    = 0x42,
	farch_lio_port_pit_command           = 0x43,

	farch_lio_port_pc_speaker            = 0x61,
};

/**
 * @}
 */

FERRO_DECLARATIONS_END;

#endif // _FERRO_CORE_X86_64_LEGACY_IO_H_
