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
 * Serial ports subsystem.
 */

#ifndef _FERRO_CORE_SERIAL_H_
#define _FERRO_CORE_SERIAL_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include <ferro/base.h>
#include <ferro/error.h>

FERRO_DECLARATIONS_BEGIN;

/**
 * @addtogroup Serial
 *
 * The serial ports subsystem.
 *
 * @{
 */

// all of the declarations in this file must be implemented by architecture-specific files

/**
 * An opaque structure representing a serial port.
 *
 * @todo Provide a way to register for event notifications (e.g. new peer, dead peer, new data, etc.).
 */
FERRO_STRUCT_FWD(fserial);

/**
 * A callback that is invoked when new data is received on the serial port.
 *
 * @param data User-defined data provided to fserial_read_notify() when registering the callback.
 */
typedef void (*fserial_read_notify_f)(void* data);

/**
 * Initializes the serial ports subsystem. Called on kernel startup.
 */
void fserial_init(void);

/**
 * Finds the serial port with the given ID.
 *
 * @returns An opaque pointer to an ::fserial object representing the serial port with the given ID, or `NULL` if none could be found.
 */
fserial_t* fserial_find(size_t id);

/**
 * Reads a single byte from the given serial port.
 *
 * @param   serial_port The serial port object representing the port to read from.
 * @param      blocking Whether to block until data arrives if none is currently available.
 * @param[out] out_byte Optional pointer in which the byte that was read will be written on success.
 *                      If this is `NULL`, the byte will be read from the serial port and then discarded.
 *
 * @retval ferr_ok               The byte was successfully read into @p out_byte.
 * @retval ferr_invalid_argument @p serial_port was not a pointer to a valid serial port object.
 * @retval ferr_temporary_outage No bytes were available to be read.
 *                               This can only be returned when @p blocking is `false`.
 */
ferr_t fserial_read(fserial_t* serial_port, bool blocking, uint8_t* out_byte);

/**
 * Writes a single byte to the given serial port.
 *
 * @param serial_port The serial port object representing the port to write to.
 * @param blocking    Whether to block until the byte has been written.
 * @param byte        The byte that to write to the serial port.
 *
 * @retval ferr_ok               The byte was successfully written.
 * @retval ferr_invalid_argument @p serial_port was not a pointer to a valid serial port object.
 * @retval ferr_temporary_outage The serial port's transmission buffer was full and the byte could not be written.
 *                               This can only be returned when @p blocking is `false`.
 */
ferr_t fserial_write(fserial_t* serial_port, bool blocking, uint8_t byte);

/**
 * Checks whether the given serial port is connected to a peer.
 *
 * @param serial_port The serial port object representing the port to check.
 *
 * @retval ferr_ok               A peer is connected on the other end of the serial port.
 * @retval ferr_temporary_outage No peer is connected on the other end of the serial port.
 * @retval ferr_invalid_argument @p serial_port was not a pointer to a valid serial port object.
 */
ferr_t fserial_connected(fserial_t* serial_port);

/**
 * Requests that the given callback be invoked when new data is received from the given serial port.
 *
 * @param serial_port The serial port object representing the port to check.
 * @param callback    The callback to invoke. A value of `NULL` here will unregister the previous callback.
 * @param data        User-defined data to pass to the callback.
 *
 * @note The callback will most likely be called from an interrupt context, but whether it actually is depends on the architecture.
 *
 * @note The callback may be invoked spuriously. In other words, there may be times when there was data available when the call was scheduled,
 *       but by the time it actually occurred, it had already been read by someone else and was gone.
 *
 * @retval ferr_ok               The callback was successfully registered.
 * @retval ferr_invalid_argument @p serial_port was not a pointer to a valid serial port object.
 */
ferr_t fserial_read_notify(fserial_t* serial_port, fserial_read_notify_f callback, void* data);

/**
 * @}
 */

FERRO_DECLARATIONS_END;

#endif // _FERRO_CORE_SERIAL_H_
