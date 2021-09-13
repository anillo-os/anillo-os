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

#include <ferro/core/serial.h>

// TODO: implement serial ports for AARCH64. this entire file is a stub at the moment.

void fserial_init(void) {

};

fserial_t* fserial_find(size_t id) {
	return NULL;
};

ferr_t fserial_read(fserial_t* serial_port, bool blocking, uint8_t* out_byte) {
	return ferr_invalid_argument;
};

ferr_t fserial_write(fserial_t* serial_port, bool blocking, uint8_t byte) {
	return ferr_invalid_argument;
};

ferr_t fserial_connected(fserial_t* serial_port) {
	return ferr_invalid_argument;
};

ferr_t fserial_read_notify(fserial_t* serial_port, fserial_read_notify_f callback, void* data) {
	return ferr_invalid_argument;
};
