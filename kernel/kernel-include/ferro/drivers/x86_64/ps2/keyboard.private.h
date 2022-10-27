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

#ifndef _FERRO_DRIVERS_x86_64_PS2_KEYBOARD_PRIVATE_H_
#define _FERRO_DRIVERS_x86_64_PS2_KEYBOARD_PRIVATE_H_

#include <ferro/drivers/x86_64/ps2/keyboard.h>
#include <ferro/drivers/keyboard.h>

#include <stdbool.h>
#include <stdint.h>

FERRO_DECLARATIONS_BEGIN;

FERRO_ENUM(uint16_t, ferro_ps2_keyboard_port) {
	ferro_ps2_keyboard_port_data    = 0x60,
	ferro_ps2_keyboard_port_status  = 0x64,
	ferro_ps2_keyboard_port_command = 0x64,
};

FERRO_ENUM(uint8_t, ferro_ps2_keyboard_status_bits) {
	ferro_ps2_keyboard_status_bit_output_full     = 1 << 0,
	ferro_ps2_keyboard_status_bit_input_full      = 1 << 1,
	ferro_ps2_keyboard_status_bit_system_flag     = 1 << 2,
	ferro_ps2_keyboard_status_bit_data_is_command = 1 << 3,
	ferro_ps2_keyboard_status_bit_timeout_error   = 1 << 6,
	ferro_ps2_keyboard_status_bit_parity_error    = 1 << 7,
};

FERRO_ENUM(uint8_t, ferro_ps2_keyboard_command) {
	ferro_ps2_keyboard_command_read_ram_byte_0          = 0x20,
	ferro_ps2_keyboard_command_write_ram_byte_0         = 0x60,
	ferro_ps2_keyboard_command_disable_second_port      = 0xa7,
	ferro_ps2_keyboard_command_enable_second_port       = 0xa8,
	ferro_ps2_keyboard_command_test_second_port         = 0xa9,
	ferro_ps2_keyboard_command_test_controller          = 0xaa,
	ferro_ps2_keyboard_command_test_first_port          = 0xab,
	ferro_ps2_keyboard_command_disable_first_port       = 0xad,
	ferro_ps2_keyboard_command_enable_first_port        = 0xae,
	ferro_ps2_keyboard_command_read_controller_input    = 0xc0,
	ferro_ps2_keyboard_command_read_controller_output   = 0xd0,
	ferro_ps2_keyboard_command_write_controller_output  = 0xd1,
	ferro_ps2_keyboard_command_write_first_port_output  = 0xd2,
	ferro_ps2_keyboard_command_write_second_port_output = 0xd3,
	ferro_ps2_keyboard_command_write_second_port_input  = 0xd4,

	ferro_ps2_keyboard_command_get_or_set_scan_code_set        = 0xf0,
	ferro_ps2_keyboard_command_enable_scanning          = 0xf4,
	ferro_ps2_keyboard_command_disable_scanning         = 0xf5,
	ferro_ps2_keyboard_command_set_default_parameters   = 0xf6,
	ferro_ps2_keyboard_command_reset                    = 0xff,
};

FERRO_ENUM(uint8_t, ferro_ps2_keyboard_misc) {
	ferro_ps2_keyboard_self_test_passed = 0xaa,
	ferro_ps2_keyboard_acknowledgement  = 0xfa,
	ferro_ps2_keyboard_resend           = 0xfe,
};

FERRO_ENUM(uint8_t, ferro_ps2_keyboard_config_bit) {
	ferro_ps2_keyboard_config_bit_first_port_interrupt_enabled  = 1 << 0,
	ferro_ps2_keyboard_config_bit_second_port_interrupt_enabled = 1 << 1,
	ferro_ps2_keyboard_config_bit_system_flag                   = 1 << 2,
	ferro_ps2_keyboard_config_bit_first_port_clock_disabled     = 1 << 4,
	ferro_ps2_keyboard_config_bit_second_port_clock_disabled    = 1 << 5,
	ferro_ps2_keyboard_config_bit_first_port_translation        = 1 << 6,
};

// 100ms
#define FERRO_PS2_KEYBOARD_TIMEOUT_NS 100000000ull

#define FERRO_PS2_KEYBOARD_MAX_RETRIES 10

FERRO_STRUCT(ferro_ps2_keyboard_state) {
	uint8_t pause_index;
	uint8_t print_screen_index;
	bool breaking_print_screen;
	bool extended;
	bool break_code;
	fkeyboard_state_t keyboard_state;

	// no locks, since this should only be accessed from the interrupt handler and only one instance of it should be running at any given time
};

FERRO_DECLARATIONS_END;

#endif // _FERRO_DRIVERS_x86_64_PS2_KEYBOARD_PRIVATE_H_
