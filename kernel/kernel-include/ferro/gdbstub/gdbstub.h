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
 * GDB stub subsystem.
 */

#ifndef _FERRO_GDBSTUB_GDBSTUB_H_
#define _FERRO_GDBSTUB_GDBSTUB_H_

#include <stdint.h>

#include <ferro/base.h>
#include <ferro/core/serial.h>
#include <ferro/core/interrupts.h>

FERRO_DECLARATIONS_BEGIN;

/**
 * @addtogroup GDB-Stub
 *
 * The GDB stub subsystem.
 *
 * @{
 */

/**
 * Initializes the GDB stub subsystem.
 *
 * @param serial_port The port to use to communicate with GDB.
 */
void fgdb_init(fserial_t* serial_port);

FERRO_WUR ferr_t fgdb_register_passthrough_handlers(fint_special_handler_f breakpoint, fint_special_handler_f single_step, fint_special_handler_f watchpoint);

/**
 * @}
 */

FERRO_DECLARATIONS_END;

#endif // _FERRO_GDBSTUB_GDBSTUB_H_
