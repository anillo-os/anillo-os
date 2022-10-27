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
#include <ferro/core/x86_64/legacy-io.h>
#include <ferro/core/x86_64/apic.h>
#include <ferro/core/interrupts.h>
#include <ferro/core/locks.h>
#include <ferro/core/panic.h>

#include <stdatomic.h>

// the actual amount of data that each buffer can hold is 1 less than this
#define INCOMING_BUFFER_SIZE 256
#define OUTGOING_BUFFER_SIZE 256

FERRO_STRUCT(fserial) {
	uint16_t io_base;

	/**
	 * A circular buffer of bytes read from the serial port.
	 *
	 * If this starts filling up, the oldest characters will be discarded to allow the new ones to be read.
	 */
	uint8_t incoming_buffer[INCOMING_BUFFER_SIZE];
	flock_spin_intsafe_t incoming_buffer_lock;
	size_t incoming_buffer_start;
	size_t incoming_buffer_end;
	flock_semaphore_t incoming_buffer_sema;

	/**
	 * Protected by #incoming_buffer_lock.
	 */
	fserial_read_notify_f read_notify;
	void* read_notify_data;

	/**
	 * A circular buffer of bytes to write to the serial port.
	 *
	 * Once this is full, new writers must wait for old bytes to be written.
	 */
	uint8_t outgoing_buffer[OUTGOING_BUFFER_SIZE];
	flock_spin_intsafe_t outgoing_buffer_lock;
	size_t outgoing_buffer_start;
	size_t outgoing_buffer_end;
	flock_semaphore_t outgoing_buffer_sema;
};

static fserial_t serial_ports[4] = {
	{ .io_base = 0x3f8 },
	{ .io_base = 0x2f8 },
	{ .io_base = 0x3e8 },
	{ .io_base = 0x2e8 },
};

static void serial_transmit_locked(fserial_t* serial_port) {
	while ((farch_lio_read_u8(serial_port->io_base + 5) & (1 << 5)) != 0) {
		if (serial_port->outgoing_buffer_start == serial_port->outgoing_buffer_end) {
			break;
		}

		uint8_t byte = serial_port->outgoing_buffer[serial_port->outgoing_buffer_start];

		serial_port->outgoing_buffer_start = (serial_port->outgoing_buffer_start + 1) % sizeof(serial_port->outgoing_buffer);

		farch_lio_write_u8(serial_port->io_base + 0, byte);

		flock_semaphore_up(&serial_port->outgoing_buffer_sema);
	}
};

static void serial_receive_locked(fserial_t* serial_port) {
	while ((farch_lio_read_u8(serial_port->io_base + 5) & 1) != 0) {
		size_t next_end = (serial_port->incoming_buffer_end + 1) % sizeof(serial_port->incoming_buffer);

		if (next_end == serial_port->incoming_buffer_start) {
			serial_port->incoming_buffer_start = (serial_port->incoming_buffer_start + 1) % sizeof(serial_port->incoming_buffer);
		}

		serial_port->incoming_buffer[serial_port->incoming_buffer_end] = farch_lio_read_u8(serial_port->io_base + 0);

		serial_port->incoming_buffer_end = next_end;

		flock_semaphore_up(&serial_port->incoming_buffer_sema);
	}
};

static void serial_interrupt(void* data, fint_frame_t* frame) {
	// TODO: is there a way to tell specifically which serial port triggered the interrupt?
	//       there are 2 interrupts, but 4 serial ports, so... ???

	for (size_t i = 0; i < sizeof(serial_ports) / sizeof(*serial_ports); ++i) {
		fserial_t* serial_port = &serial_ports[i];
		uint8_t interrupt_reason = farch_lio_read_u8(serial_port->io_base + 2);
		uint8_t line_status = farch_lio_read_u8(serial_port->io_base + 5);

		if ((interrupt_reason & 1) != 0) {
			// if the pending interrupt bit is set, this is not the port that triggered the interrupt
			continue;
		}

		if ((line_status & 1) != 0) {
			// receive buffer non-empty
			fserial_read_notify_f callback = NULL;
			void* callback_data = NULL;

			flock_spin_intsafe_lock(&serial_port->incoming_buffer_lock);
			serial_receive_locked(serial_port);
			callback = serial_port->read_notify;
			callback_data = serial_port->read_notify_data;
			flock_spin_intsafe_unlock(&serial_port->incoming_buffer_lock);

			if (callback) {
				callback(callback_data);
			}
		}

		if ((line_status & (1 << 5)) != 0) {
			// transmit buffer empty
			flock_spin_intsafe_lock(&serial_port->outgoing_buffer_lock);
			serial_transmit_locked(serial_port);
			flock_spin_intsafe_unlock(&serial_port->outgoing_buffer_lock);
		}
	}

	farch_apic_signal_eoi();
};

void fserial_init(void) {
	uint8_t interrupt_number;

	if (farch_int_register_next_available(serial_interrupt, NULL, &interrupt_number) != ferr_ok) {
		fpanic("Failed to register serial port interrupt handler");
	}

	if (farch_ioapic_map_legacy(3, interrupt_number) != ferr_ok) {
		fpanic("Failed to map first serial port interrupt with IOAPIC");
	}

	if (farch_ioapic_map_legacy(4, interrupt_number) != ferr_ok) {
		fpanic("Failed to map second serial port interrupt with IOAPIC");
	}

	if (farch_ioapic_unmask_legacy(3) != ferr_ok) {
		fpanic("Failed to unmask first serial port interrupt with IOAPIC");
	}

	if (farch_ioapic_unmask_legacy(4) != ferr_ok) {
		fpanic("Failed to unmask second serial port interrupt with IOAPIC");
	}

	for (size_t i = 0; i < sizeof(serial_ports) / sizeof(*serial_ports); ++i) {
		fserial_t* serial_port = &serial_ports[i];

		flock_semaphore_init(&serial_port->incoming_buffer_sema, 0);
		flock_semaphore_init(&serial_port->outgoing_buffer_sema, sizeof(serial_port->outgoing_buffer) - 1);

		// disable all interrupts (temporarily)
		farch_lio_write_u8(serial_port->io_base + 1, 0x00);

		// use the highest baud rate (115200)
		farch_lio_write_u8(serial_port->io_base + 3, 1 << 7); // set DLAB
		farch_lio_write_u8(serial_port->io_base + 0, 1); // divisor, low byte
		farch_lio_write_u8(serial_port->io_base + 1, 0); // divisor, high byte
		farch_lio_write_u8(serial_port->io_base + 3, 0); // clear DLAB

		// 8 bits, no parity bits, 1 stop bit
		farch_lio_write_u8(serial_port->io_base + 3, 0x03);

		// enable the FIFOs, clear any data leftover, and set the interrupt trigger to about a quarter
		farch_lio_write_u8(serial_port->io_base + 2, (1 << 0) | (1 << 1) | (1 << 2) | (1 << 6));

		// enable DTR, RTS, and interrupt output
		farch_lio_write_u8(serial_port->io_base + 4, (1 << 0) | (1 << 1) | (1 << 3));

		// enable all interrupt types
		farch_lio_write_u8(serial_port->io_base + 1, (1 << 0) | (1 << 1) | (1 << 2) | (1 << 3));
	}
};

fserial_t* fserial_find(size_t id) {
	if (id >= sizeof(serial_ports) / sizeof(*serial_ports)) {
		return NULL;
	}
	return &serial_ports[id];
};

ferr_t fserial_read(fserial_t* serial_port, bool blocking, uint8_t* out_byte) {
	if (serial_port < &serial_ports[0] || serial_port > &serial_ports[sizeof(serial_ports) / sizeof(*serial_ports)]) {
		return ferr_invalid_argument;
	}

	flock_spin_intsafe_lock(&serial_port->incoming_buffer_lock);

	// first, see if there's something already in the buffer
	if (flock_semaphore_try_down(&serial_port->incoming_buffer_sema) == ferr_ok) {
		goto out_success;
	}

	// okay, so it's empty. try receiving something into the buffer now.
	serial_receive_locked(serial_port);

	// now try the buffer again.
	if (flock_semaphore_try_down(&serial_port->incoming_buffer_sema) != ferr_ok) {
		// okay, so it's still empty. we either have to block (if we're allowed to) or else return an error.

		flock_spin_intsafe_unlock(&serial_port->incoming_buffer_lock);

		if (blocking) {
			flock_semaphore_down(&serial_port->incoming_buffer_sema);
			flock_spin_intsafe_lock(&serial_port->incoming_buffer_lock);
		} else {
			return ferr_temporary_outage;
		}
	}

out_success:
	if (out_byte) {
		*out_byte = serial_port->incoming_buffer[serial_port->incoming_buffer_start];
	}
	serial_port->incoming_buffer_start = (serial_port->incoming_buffer_start + 1) % sizeof(serial_port->incoming_buffer);

	flock_spin_intsafe_unlock(&serial_port->incoming_buffer_lock);
	return ferr_ok;
};

ferr_t fserial_write(fserial_t* serial_port, bool blocking, uint8_t byte) {
	size_t next_end;

	if (serial_port < &serial_ports[0] || serial_port > &serial_ports[sizeof(serial_ports) / sizeof(*serial_ports)]) {
		return ferr_invalid_argument;
	}

	flock_spin_intsafe_lock(&serial_port->outgoing_buffer_lock);

	// first, see if there's already space in the buffer
	if (flock_semaphore_try_down(&serial_port->outgoing_buffer_sema) == ferr_ok) {
		goto out_success;
	}

	// okay, so it's full. try sending something from the buffer now.
	serial_transmit_locked(serial_port);

	// now try the buffer again.
	if (flock_semaphore_try_down(&serial_port->outgoing_buffer_sema) != ferr_ok) {
		// okay, so it's still full. we either have to block (if we're allowed to) or else return an error.

		flock_spin_intsafe_unlock(&serial_port->outgoing_buffer_lock);

		if (blocking) {
			flock_semaphore_down(&serial_port->outgoing_buffer_sema);
			flock_spin_intsafe_lock(&serial_port->outgoing_buffer_lock);
		} else {
			return ferr_temporary_outage;
		}
	}

out_success:
	next_end = (serial_port->outgoing_buffer_end + 1) % sizeof(serial_port->outgoing_buffer);

	if (next_end == serial_port->outgoing_buffer_start) {
		//serial_port->outgoing_buffer_start = (serial_port->outgoing_buffer_start + 1) % sizeof(serial_port->outgoing_buffer);
		fpanic("Need to overwrite an outgoing buffer character! This should never occur!");
	}

	serial_port->outgoing_buffer[serial_port->outgoing_buffer_end] = byte;

	serial_port->outgoing_buffer_end = next_end;

	// now try to transmit what we have
	serial_transmit_locked(serial_port);

	flock_spin_intsafe_unlock(&serial_port->outgoing_buffer_lock);
	return ferr_ok;
};

ferr_t fserial_connected(fserial_t* serial_port) {
	if (serial_port < &serial_ports[0] || serial_port > &serial_ports[sizeof(serial_ports) / sizeof(*serial_ports)]) {
		return ferr_invalid_argument;
	}

	return ((farch_lio_read_u8(serial_port->io_base + 6) & (1 << 7)) != 0) ? ferr_ok : ferr_temporary_outage;
};

ferr_t fserial_read_notify(fserial_t* serial_port, fserial_read_notify_f callback, void* data) {
	if (serial_port < &serial_ports[0] || serial_port > &serial_ports[sizeof(serial_ports) / sizeof(*serial_ports)]) {
		return ferr_invalid_argument;
	}

	flock_spin_intsafe_lock(&serial_port->incoming_buffer_lock);
	serial_port->read_notify = callback;
	serial_port->read_notify_data = data;
	flock_spin_intsafe_unlock(&serial_port->incoming_buffer_lock);

	return ferr_ok;
};
