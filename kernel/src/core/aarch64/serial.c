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
#include <ferro/core/panic.h>
#include <ferro/core/paging.h>

// PL011 UART driver, based on https://krinkinmu.github.io/2020/11/29/PL011.html and https://github.com/krinkinmu/aarch64

#define RESERVED_HELPER2(x, y) uint8_t reserved ## y [x]
#define RESERVED_HELPER(x, y) RESERVED_HELPER2(x, y)
#define RESERVED(x) RESERVED_HELPER(x, __COUNTER__)

FERRO_PACKED_STRUCT(farch_pl011_registers) {
	volatile uint32_t data;
	volatile uint32_t receive_status;
	RESERVED(16);
	volatile uint32_t flags;
	RESERVED(4);
	volatile uint32_t irda_low_power_counter;
	volatile uint32_t integer_baud_rate;
	volatile uint32_t fractional_baud_rate;
	volatile uint32_t line_control;
	volatile uint32_t control;
	volatile uint32_t interrupt_fifo_level_select;
	volatile uint32_t interrupt_mask_set_or_clear;
	volatile uint32_t raw_interrupt_status;
	volatile uint32_t masked_interrupt_status;
	volatile uint32_t interrupt_clear;
	volatile uint32_t dma_control;
	RESERVED(3988);
	volatile uint32_t periph_id[4];
	volatile uint32_t pcell_id[4];
};

FERRO_PACKED_STRUCT(farch_pl011) {
	uint64_t baudrate;
	uint64_t base_clock; // in Hz
	volatile farch_pl011_registers_t* registers;
};

FERRO_OPTIONS(uint16_t, farch_pl011_flags) {
	farch_pl011_flag_data_carrier_detect = 1 << 2,
	farch_pl011_flag_busy                = 1 << 3,
};

FERRO_OPTIONS(uint16_t, farch_pl011_line_control_flags) {
	farch_pl011_line_control_flag_fifo_enable = 1 << 4,
};

FERRO_OPTIONS(uint16_t, farch_pl011_control_flags) {
	farch_pl011_control_flag_enable          = 1 << 0,
	farch_pl011_control_flag_transmit_enable = 1 << 8,
};

static farch_pl011_t controller = {
	.baudrate = 115200,

	// QEMU's PL011 clock rate
	.base_clock = 24000000,
};

void fserial_init(void) {
	// QEMU's PL011 base address
	fpanic_status(fpage_map_kernel_any((void*)0x9000000, fpage_round_up_to_page_count(sizeof(*controller.registers)), (void*)&controller.registers, fpage_flag_no_cache));

	// disable the controller first
	controller.registers->control &= ~farch_pl011_control_flag_enable;

	// wait for pending transmissions
	while ((controller.registers->flags & farch_pl011_flag_busy) != 0);

	// disable the FIFOs
	controller.registers->line_control &= ~farch_pl011_line_control_flag_fifo_enable;

	// mask all interrupts
	controller.registers->interrupt_mask_set_or_clear = 0x7ff;

	// clear all interrupts
	controller.registers->interrupt_clear = 0x7ff;

	// disable DMA
	controller.registers->dma_control = 0;

	// calculate the divisor
	uint64_t divisor = ((8 * controller.base_clock) + controller.baudrate)  / (2 * controller.baudrate);

	controller.registers->integer_baud_rate = (divisor >> 6) & 0xffff;
	controller.registers->fractional_baud_rate = divisor & 0x3f;

	// 8 data bits, 1 stop bit
	controller.registers->line_control = 3 << 5;

	// enable transmission (we don't care about reception yet)
	controller.registers->control = farch_pl011_control_flag_transmit_enable;

	// now enable the UART
	controller.registers->control |= farch_pl011_control_flag_enable;
};

fserial_t* fserial_find(size_t id) {
	if (id == 0) {
		return (void*)&controller;
	}

	return NULL;
};

ferr_t fserial_read(fserial_t* serial_port, bool blocking, uint8_t* out_byte) {
	farch_pl011_t* controller = (void*)serial_port;

	return ferr_unsupported;
};

ferr_t fserial_write(fserial_t* serial_port, bool blocking, uint8_t byte) {
	farch_pl011_t* controller = (void*)serial_port;

	if (!blocking) {
		return ferr_unsupported;
	}

	// wait for pending transmissions
	while ((controller->registers->flags & farch_pl011_flag_busy) != 0);

	controller->registers->data = byte;

	// wait for it to finish sending
	while ((controller->registers->flags & farch_pl011_flag_busy) != 0);

	return ferr_ok;
};

ferr_t fserial_connected(fserial_t* serial_port) {
	farch_pl011_t* controller = (void*)serial_port;

	return (controller->registers->flags & farch_pl011_flag_data_carrier_detect) == 0 ? ferr_ok : ferr_temporary_outage;
};

ferr_t fserial_read_notify(fserial_t* serial_port, fserial_read_notify_f callback, void* data) {
	farch_pl011_t* controller = (void*)serial_port;

	return ferr_unsupported;
};
