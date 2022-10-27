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

#include <ferro/drivers/x86_64/ps2/keyboard.private.h>
#include <ferro/core/x86_64/apic.h>
#include <ferro/core/x86_64/legacy-io.h>
#include <ferro/core/console.h>
#include <ferro/drivers/keyboard.h>
#include <ferro/core/interrupts.h>
#include <ferro/core/timers.h>
#include <ferro/core/threads.h>

FERRO_WUR
static ferr_t ferro_ps2_keyboard_wait_for_write(void) {
	ftimers_timestamp_t start;
	ftimers_timestamp_t end;
	uint64_t delta_ns = 0;

	if (ftimers_timestamp_read(&start) != ferr_ok) {
		return ferr_unknown;
	}

	end = start;

	while ((farch_lio_read_u8(ferro_ps2_keyboard_port_status) & ferro_ps2_keyboard_status_bit_input_full) != 0 && delta_ns < FERRO_PS2_KEYBOARD_TIMEOUT_NS) {
		if (ftimers_timestamp_read(&end) != ferr_ok) {
			return ferr_unknown;
		}

		if (ftimers_timestamp_delta_to_ns(start, end, &delta_ns) != ferr_ok) {
			return ferr_unknown;
		}
	}

	if (delta_ns >= FERRO_PS2_KEYBOARD_TIMEOUT_NS) {
		return ferr_timed_out;
	} else {
		return ferr_ok;
	}
};

FERRO_WUR
static ferr_t ferro_ps2_keyboard_wait_for_read(void) {
	ftimers_timestamp_t start;
	ftimers_timestamp_t end;
	uint64_t delta_ns = 0;

	if (ftimers_timestamp_read(&start) != ferr_ok) {
		return ferr_unknown;
	}

	end = start;

	while ((farch_lio_read_u8(ferro_ps2_keyboard_port_status) & ferro_ps2_keyboard_status_bit_output_full) == 0 && delta_ns < FERRO_PS2_KEYBOARD_TIMEOUT_NS) {
		if (ftimers_timestamp_read(&end) != ferr_ok) {
			return ferr_unknown;
		}

		if (ftimers_timestamp_delta_to_ns(start, end, &delta_ns) != ferr_ok) {
			return ferr_unknown;
		}
	}

	if (delta_ns >= FERRO_PS2_KEYBOARD_TIMEOUT_NS) {
		return ferr_timed_out;
	} else {
		return ferr_ok;
	}
};

FERRO_WUR
static ferr_t ferro_ps2_keyboard_perform_command(ferro_ps2_keyboard_command_t command, bool has_input, uint8_t input, bool has_output, uint8_t* out_output) {
	ferr_t status = ferr_ok;

	status = ferro_ps2_keyboard_wait_for_write();
	if (status != ferr_ok) {
		goto out;
	}

	farch_lio_write_u8(ferro_ps2_keyboard_port_command, command);

	if (has_input) {
		status = ferro_ps2_keyboard_wait_for_write();
		if (status != ferr_ok) {
			goto out;
		}
		farch_lio_write_u8(ferro_ps2_keyboard_port_data, input);
	}

	if (has_output) {
		uint8_t output;

		status = ferro_ps2_keyboard_wait_for_read();
		if (status != ferr_ok) {
			goto out;
		}

		output = farch_lio_read_u8(ferro_ps2_keyboard_port_data);

		if (out_output) {
			*out_output = output;
		}
	}

out:
	return status;
};

static void ferro_ps2_keyboard_clear_data(void) {
	while ((farch_lio_read_u8(ferro_ps2_keyboard_port_status) & ferro_ps2_keyboard_status_bit_output_full) != 0) {
		farch_lio_read_u8(ferro_ps2_keyboard_port_data);
	}
};

/*
01: F9
03: F5
04: F3
05: F1
06: F2
07: F12
09: F10
0a: F8
0b: F6
0c: F4
0d: tab
0e: ` (back tick)
11: left alt
12: left shift
14: left control
15: Q
16: 1
1a: Z
1b: S
1c: A
1d: W
1e: 2
21: C
22: X
23: D
24: E
25: 4
26: 3
29: space
2a: V
2b: F
2c: T
2d: R
2e: 5
31: N
32: B
33: H
34: G
35: Y
36: 6
3a: M
3b: J
3c: U
3d: 7
3e: 8
41: ,
42: K
43: I
44: O
45: 0 (zero)
46: 9
49: .
4a: /
4b: L
4c:  ;
4d: P
4e: -
52: '
54: [
55: =
58: CapsLock
59: right shift
5a: enter
5b: ]
5d: \
66: backspace
69: (keypad) 1
6b: (keypad) 4
6c: (keypad) 7
70: (keypad) 0
71: (keypad) .
72: (keypad) 2
73: (keypad) 5
74: (keypad) 6
75: (keypad) 8
76: escape
77: NumberLock
78: F11
79: (keypad) +
7a: (keypad) 3
7b: (keypad) -
7c: (keypad) *
7d: (keypad) 9
7e: ScrollLock
83: F7
*/

static fkeyboard_key_t standard_keycode_map[] = {
	[0x01] = fkeyboard_key_f9,
	[0x03] = fkeyboard_key_f5,
	[0x04] = fkeyboard_key_f3,
	[0x05] = fkeyboard_key_f1,
	[0x06] = fkeyboard_key_f2,
	[0x07] = fkeyboard_key_f12,
	[0x09] = fkeyboard_key_f10,
	[0x0a] = fkeyboard_key_f8,
	[0x0b] = fkeyboard_key_f6,
	[0x0c] = fkeyboard_key_f4,
	[0x0d] = fkeyboard_key_tab,
	[0x0e] = fkeyboard_key_grave_accent,
	[0x11] = fkeyboard_key_left_alt,
	[0x12] = fkeyboard_key_left_shift,
	[0x14] = fkeyboard_key_left_control,
	[0x15] = fkeyboard_key_letter_q,
	[0x16] = fkeyboard_key_1,
	[0x1a] = fkeyboard_key_letter_z,
	[0x1b] = fkeyboard_key_letter_s,
	[0x1c] = fkeyboard_key_letter_a,
	[0x1d] = fkeyboard_key_letter_w,
	[0x1e] = fkeyboard_key_2,
	[0x21] = fkeyboard_key_letter_c,
	[0x22] = fkeyboard_key_letter_x,
	[0x23] = fkeyboard_key_letter_d,
	[0x24] = fkeyboard_key_letter_e,
	[0x25] = fkeyboard_key_4,
	[0x26] = fkeyboard_key_3,
	[0x29] = fkeyboard_key_space,
	[0x2a] = fkeyboard_key_letter_v,
	[0x2b] = fkeyboard_key_letter_f,
	[0x2c] = fkeyboard_key_letter_t,
	[0x2d] = fkeyboard_key_letter_r,
	[0x2e] = fkeyboard_key_5,
	[0x31] = fkeyboard_key_letter_n,
	[0x32] = fkeyboard_key_letter_b,
	[0x33] = fkeyboard_key_letter_h,
	[0x34] = fkeyboard_key_letter_g,
	[0x35] = fkeyboard_key_letter_y,
	[0x36] = fkeyboard_key_6,
	[0x3a] = fkeyboard_key_letter_m,
	[0x3b] = fkeyboard_key_letter_j,
	[0x3c] = fkeyboard_key_letter_u,
	[0x3d] = fkeyboard_key_7,
	[0x3e] = fkeyboard_key_8,
	[0x41] = fkeyboard_key_comma,
	[0x42] = fkeyboard_key_letter_k,
	[0x43] = fkeyboard_key_letter_i,
	[0x44] = fkeyboard_key_letter_o,
	[0x45] = fkeyboard_key_0,
	[0x46] = fkeyboard_key_9,
	[0x49] = fkeyboard_key_dot,
	[0x4a] = fkeyboard_key_slash,
	[0x4b] = fkeyboard_key_letter_l,
	[0x4c] = fkeyboard_key_semicolon,
	[0x4d] = fkeyboard_key_letter_p,
	[0x4e] = fkeyboard_key_minus,
	[0x52] = fkeyboard_key_apostrophe,
	[0x54] = fkeyboard_key_opening_bracket,
	[0x55] = fkeyboard_key_equals,
	[0x58] = fkeyboard_key_caps_lock,
	[0x59] = fkeyboard_key_right_shift,
	[0x5a] = fkeyboard_key_return,
	[0x5b] = fkeyboard_key_closing_bracket,
	[0x5d] = fkeyboard_key_backslash,
	[0x66] = fkeyboard_key_backspace,
	[0x69] = fkeyboard_key_keypad_1,
	[0x6b] = fkeyboard_key_keypad_4,
	[0x6c] = fkeyboard_key_keypad_7,
	[0x70] = fkeyboard_key_keypad_0,
	[0x71] = fkeyboard_key_keypad_dot,
	[0x72] = fkeyboard_key_keypad_2,
	[0x73] = fkeyboard_key_keypad_5,
	[0x74] = fkeyboard_key_keypad_6,
	[0x75] = fkeyboard_key_keypad_8,
	[0x76] = fkeyboard_key_escape,
	[0x77] = fkeyboard_key_num_lock,
	[0x78] = fkeyboard_key_f11,
	[0x79] = fkeyboard_key_keypad_plus,
	[0x7a] = fkeyboard_key_keypad_3,
	[0x7b] = fkeyboard_key_keypad_minus,
	[0x7c] = fkeyboard_key_keypad_times,
	[0x7d] = fkeyboard_key_keypad_9,
	[0x7e] = fkeyboard_key_scroll_lock,
	[0x83] = fkeyboard_key_f7,
};

/*
10: (multimedia) WWW search
11: right alt
14: right control
15: (multimedia) previous track
18: (multimedia) WWW favourites
1f: left GUI
20: (multimedia) WWW refresh
21: (multimedia) volume down
23: (multimedia) mute
27: right GUI
28: (multimedia) WWW stop
2b: (multimedia) calculator
2f: apps
30: (multimedia) WWW forward
32: (multimedia) volume up
34: (multimedia) play/pause
37: (ACPI) power
38: (multimedia) WWW back
3a: (multimedia) WWW home
3b: (multimedia) stop
3f: (ACPI) sleep
40: (multimedia) my computer
48: (multimedia) email
4a: (keypad) /
4d: (multimedia) next track
50: (multimedia) media select
5a: (keypad) enter
5e: (ACPI) wake
69: end
6b: cursor left
6c: home
70: insert
71: delete
72: cursor down
74: cursor right
75: cursor up
7a: page down
7d: page up
*/

static fkeyboard_key_t extended_keycode_map[] = {
	//[0x10] = fkeyboard_key_,
	[0x11] = fkeyboard_key_right_alt,
	[0x14] = fkeyboard_key_right_control,
	//[0x15] = fkeyboard_key_,
	//[0x18] = fkeyboard_key_,
	[0x1f] = fkeyboard_key_left_meta,
	//[0x20] = fkeyboard_key_,
	//[0x21] = fkeyboard_key_,
	//[0x23] = fkeyboard_key_,
	[0x27] = fkeyboard_key_right_meta,
	//[0x28] = fkeyboard_key_,
	//[0x2b] = fkeyboard_key_,
	[0x2f] = fkeyboard_key_application,
	//[0x30] = fkeyboard_key_,
	//[0x32] = fkeyboard_key_,
	//[0x34] = fkeyboard_key_,
	//[0x37] = fkeyboard_key_,
	//[0x38] = fkeyboard_key_,
	//[0x3a] = fkeyboard_key_,
	//[0x3b] = fkeyboard_key_,
	//[0x3f] = fkeyboard_key_,
	//[0x40] = fkeyboard_key_,
	//[0x48] = fkeyboard_key_,
	[0x4a] = fkeyboard_key_keypad_divide,
	//[0x4d] = fkeyboard_key_,
	//[0x50] = fkeyboard_key_,
	[0x5a] = fkeyboard_key_keypad_enter,
	//[0x5e] = fkeyboard_key_,
	[0x69] = fkeyboard_key_end,
	[0x6b] = fkeyboard_key_left_arrow,
	[0x6c] = fkeyboard_key_home,
	[0x70] = fkeyboard_key_insert,
	[0x71] = fkeyboard_key_delete,
	[0x72] = fkeyboard_key_down_arrow,
	[0x74] = fkeyboard_key_right_arrow,
	[0x75] = fkeyboard_key_up_arrow,
	[0x7a] = fkeyboard_key_page_down,
	[0x7d] = fkeyboard_key_page_up,
};

/*
special keycode sequences that are handled with special logic in the interrupt handler:
e1 14 77 e1 f0 14 f0 77: pause pressed (no corresponding release sequence)
e0 12 e0 7c: print screen pressed
e0 f0 7c e0 f0 12: print screen released
*/

static void ferro_ps2_keyboard_state_reset(ferro_ps2_keyboard_state_t* state) {
	state->pause_index = 0;
	state->print_screen_index = 0;
	state->breaking_print_screen = false;
	state->break_code = false;
	state->extended = false;
};

static void ferro_ps2_keyboard_state_reset_invalid(ferro_ps2_keyboard_state_t* state) {
	ferro_ps2_keyboard_state_reset(state);

	// also reset the key state if we've encountered an invalid state
	fkeyboard_update_init(&state->keyboard_state);
	fkeyboard_update(&state->keyboard_state);
};

static void ferro_ps2_keyboard_interrupt_handler(void* context, fint_frame_t* frame) {
	ferro_ps2_keyboard_state_t* ps2_state = context;

	while ((farch_lio_read_u8(ferro_ps2_keyboard_port_status) & ferro_ps2_keyboard_status_bit_output_full) != 0) {
		uint8_t keycode = farch_lio_read_u8(ferro_ps2_keyboard_port_data);

		//fconsole_logf("ps2-keyboard: interrupt received with keycode=%02x\n", keycode);

		if (ps2_state->pause_index > 0) {
			uint8_t expected_keycode = 0;

			switch (ps2_state->pause_index) {
				case 1:
					expected_keycode = 0x14;
					break;
				case 2:
					expected_keycode = 0x77;
					break;
				case 3:
					expected_keycode = 0xe1;
					break;
				case 4:
					expected_keycode = 0xf0;
					break;
				case 5:
					expected_keycode = 0x14;
					break;
				case 6:
					expected_keycode = 0xf0;
					break;
				case 7:
					expected_keycode = 0x77;
					break;
			}

			if (keycode != expected_keycode) {
				// invalid state; reset it and discard this keycode
				ferro_ps2_keyboard_state_reset_invalid(ps2_state);
				continue;
			}

			++ps2_state->pause_index;

			if (ps2_state->pause_index == 8) {
				ferro_ps2_keyboard_state_reset(ps2_state);

				fkeyboard_update_add(&ps2_state->keyboard_state, fkeyboard_key_print_screen);

				fkeyboard_update(&ps2_state->keyboard_state);

				// pause always acts as though it is immediately released

				fkeyboard_update_remove(&ps2_state->keyboard_state, fkeyboard_key_print_screen);

				fkeyboard_update(&ps2_state->keyboard_state);
			}
		} else if (keycode == 0xe1) {
			if (ps2_state->pause_index != 0) {
				// invalid state; reset it and discard this keycode
				ferro_ps2_keyboard_state_reset_invalid(ps2_state);
				continue;
			}

			ps2_state->pause_index = 1;
		} else if (keycode == 0xe0) {
			if (ps2_state->break_code || ps2_state->extended) {
				// invalid state; reset it and discard this keycode
				ferro_ps2_keyboard_state_reset_invalid(ps2_state);
				continue;
			}

			ps2_state->extended = true;
		} else if (keycode == 0xf0) {
			if (ps2_state->break_code) {
				// invalid state; reset it and discard this keycode
				ferro_ps2_keyboard_state_reset_invalid(ps2_state);
				continue;
			}

			ps2_state->break_code = true;
		} else {
			fkeyboard_key_t key = fkeyboard_key_invalid;

			if (ps2_state->extended) {
				if (keycode == 0x12 || keycode == 0x7c) {
					// 0x12 is the first half of print screen when making a key and it's the second half when breaking a key
					// 0x7c is the opposite (second half when making a key, first half when breaking a key)
					bool is_first_half = (keycode == 0x12) ? !ps2_state->break_code : ps2_state->break_code;

					if (
						(is_first_half && ps2_state->print_screen_index != 0) ||
						(!is_first_half && ps2_state->print_screen_index != 1)
					) {
						// invalid state; reset it and discard this keycode
						ferro_ps2_keyboard_state_reset_invalid(ps2_state);
						continue;
					}

					if (is_first_half) {
						ps2_state->breaking_print_screen = ps2_state->break_code;
					} else if (ps2_state->breaking_print_screen != ps2_state->break_code) {
						// invalid state; reset it and discard this keycode
						ferro_ps2_keyboard_state_reset_invalid(ps2_state);
						continue;
					}

					++ps2_state->print_screen_index;

					if (ps2_state->print_screen_index == 2) {
						fconsole_logf("ps2-keyboard: breaking print screen? %s", ps2_state->breaking_print_screen ? "yes" : "no");

						if (ps2_state->breaking_print_screen) {
							fkeyboard_update_remove(&ps2_state->keyboard_state, fkeyboard_key_print_screen);
						} else {
							fkeyboard_update_add(&ps2_state->keyboard_state, fkeyboard_key_print_screen);
						}

						ferro_ps2_keyboard_state_reset(ps2_state);

						fkeyboard_update(&ps2_state->keyboard_state);
					} else {
						ps2_state->extended = false;
						ps2_state->break_code = false;
					}

					continue;
				} else if (ps2_state->print_screen_index > 0) {
					// invalid state; reset it and discard this keycode
					ferro_ps2_keyboard_state_reset_invalid(ps2_state);
					continue;
				}

				if (keycode >= sizeof(extended_keycode_map) || extended_keycode_map[keycode] == fkeyboard_key_invalid) {
					// invalid keycode; ignore it and reset the state
					ferro_ps2_keyboard_state_reset_invalid(ps2_state);
					continue;
				}

				key = extended_keycode_map[keycode];
			} else {
				if (keycode >= sizeof(standard_keycode_map) || standard_keycode_map[keycode] == fkeyboard_key_invalid) {
					// invalid keycode; ignore it and reset the state
					ferro_ps2_keyboard_state_reset_invalid(ps2_state);
					continue;
				}

				key = standard_keycode_map[keycode];
			}

			if (ps2_state->break_code) {
				fkeyboard_update_remove(&ps2_state->keyboard_state, key);
			} else {
				fkeyboard_update_add(&ps2_state->keyboard_state, key);
			}

			ferro_ps2_keyboard_state_reset(ps2_state);

			fkeyboard_update(&ps2_state->keyboard_state);
		}
	}

	farch_apic_signal_eoi();
};

// DO NOT USE DIRECTLY OUTSIDE ferro_ps2_keyboard_init()
static ferro_ps2_keyboard_state_t global_ps2_state = {0};

void ferro_ps2_keyboard_init(void) {
	uint8_t config;
	uint8_t tmp;
	uint8_t interrupt_number;
	bool test_ok;
	bool command_ok;
	ferro_ps2_keyboard_state_t* ps2_state = &global_ps2_state;

	fkeyboard_update_init(&ps2_state->keyboard_state);

	if (farch_int_register_next_available(ferro_ps2_keyboard_interrupt_handler, ps2_state, &interrupt_number) != ferr_ok) {
		fconsole_log("ps2-keyboard: failed to register interrupt handler\n");
		return;
	}

	if (farch_ioapic_map_legacy(1, interrupt_number) != ferr_ok) {
		fconsole_logf("ps2-keyboard: failed to map legacy IRQ #1 to interrupt #%u\n", interrupt_number);
		return;
	}

	fconsole_logf("ps2-keyboard: mapped legacy IRQ #1 to interrupt #%u\n", interrupt_number);

	// disable the keyboard (and mouse, if present)
	if (ferro_ps2_keyboard_perform_command(ferro_ps2_keyboard_command_disable_first_port, false, 0, false, NULL) != ferr_ok) {
		fconsole_log("ps2-keyboard: failed to disable keyboard\n");
		return;
	}
	if (ferro_ps2_keyboard_perform_command(ferro_ps2_keyboard_command_disable_second_port, false, 0, false, NULL) != ferr_ok) {
		fconsole_log("ps2-keyboard: failed to disable mouse\n");
		return;
	}

	// discard any data that may be in the data port
	ferro_ps2_keyboard_clear_data();

	// read the current config
	if (ferro_ps2_keyboard_perform_command(ferro_ps2_keyboard_command_read_ram_byte_0, false, 0, true, &config) != ferr_ok) {
		fconsole_log("ps2-keyboard: failed to read config (1)\n");
		return;
	}

	fconsole_logf("ps2-keyboard: current config = 0x%02x\n", config);

	// disable interrupts and translation
	config &= ~(ferro_ps2_keyboard_config_bit_first_port_interrupt_enabled | ferro_ps2_keyboard_config_bit_second_port_interrupt_enabled | ferro_ps2_keyboard_config_bit_first_port_translation);

	// write the new config
	if (ferro_ps2_keyboard_perform_command(ferro_ps2_keyboard_command_write_ram_byte_0, true, config, false, NULL) != ferr_ok) {
		fconsole_log("ps2-keyboard: failed to write config (1)\n");
		return;
	}

	test_ok = false;
	for (size_t i = 0; i < FERRO_PS2_KEYBOARD_MAX_RETRIES; ++i) {
		ferr_t status = ferro_ps2_keyboard_perform_command(ferro_ps2_keyboard_command_test_controller, false, 0, true, &tmp);

		if (status != ferr_ok) {
			fconsole_logf("ps2-keyboard: failed to perform controller self-test (%d: %s)\n", status, ferr_name(status));
			continue;
		}

		if (tmp != 0x55) {
			fconsole_logf("ps2-keyboard: controller self-test failed (byte = %02x)\n", tmp);
			continue;
		}

		test_ok = true;

		break;
	}

	if (!test_ok) {
		fconsole_logf("ps2-keyboard: controller self-test attempts exhausted\n");
		return;
	} else {
		fconsole_logf("ps2-keyboard: controller self-test passed\n");
	}

	ferro_ps2_keyboard_clear_data();

	// write the config again, just in case the controller was reset
	if (ferro_ps2_keyboard_perform_command(ferro_ps2_keyboard_command_write_ram_byte_0, true, config, false, NULL) != ferr_ok) {
		fconsole_log("ps2-keyboard: failed to write config (2)\n");
		return;
	}

	// test the keyboard port
	test_ok = false;
	for (size_t i = 0; i < FERRO_PS2_KEYBOARD_MAX_RETRIES; ++i) {
		ferr_t status = ferro_ps2_keyboard_perform_command(ferro_ps2_keyboard_command_test_first_port, false, 0, true, &tmp);

		if (status != ferr_ok) {
			fconsole_logf("ps2-keyboard: failed to perform keyboard port test (%d %s)\n", status, ferr_name(status));
			continue;
		}

		if (tmp != 0) {
			fconsole_logf("ps2-keyboard: port test failed (byte = %02x)\n", tmp);
			continue;
		}

		test_ok = true;
		break;
	}

	if (!test_ok) {
		fconsole_logf("ps2-keyboard: port self-test attempts exhausted\n");
		return;
	} else {
		fconsole_logf("ps2-keyboard: port self-test passed\n");
	}

	ferro_ps2_keyboard_clear_data();

	// enable the keyboard
	if (ferro_ps2_keyboard_perform_command(ferro_ps2_keyboard_command_enable_first_port, false, 0, false, NULL) != ferr_ok) {
		fconsole_log("ps2-keyboard: failed to enable keyboard\n");
		return;
	}

	test_ok = false;
	for (size_t i = 0; i < FERRO_PS2_KEYBOARD_MAX_RETRIES; ++i) {
		ferr_t status;

		ferro_ps2_keyboard_clear_data();

		// send a reset
		status = ferro_ps2_keyboard_wait_for_write();
		if (status != ferr_ok) {
			fconsole_logf("ps2-keyboard: failed to send reset (command write: %d: %s)\n", status, ferr_name(status));
			continue;
		}
		farch_lio_write_u8(ferro_ps2_keyboard_port_data, ferro_ps2_keyboard_command_reset);

		// read the first byte
		status = ferro_ps2_keyboard_wait_for_read();
		if (status != ferr_ok) {
			fconsole_logf("ps2-keyboard: failed to send reset (response read 1: %d: %s)\n", status, ferr_name(status));
			continue;
		}
		tmp = farch_lio_read_u8(ferro_ps2_keyboard_port_data);
		if (tmp != ferro_ps2_keyboard_self_test_passed && tmp != ferro_ps2_keyboard_acknowledgement) {
			fconsole_logf("ps2-keyboard: keyboard self-test failed (first byte = %02x)\n", tmp);
			continue;
		}

		// read the second byte
		status = ferro_ps2_keyboard_wait_for_read();
		if (status != ferr_ok) {
			fconsole_logf("ps2-keyboard: failed to send reset (response read 2: %d: %s)\n", status, ferr_name(status));
			continue;
		}
		tmp = farch_lio_read_u8(ferro_ps2_keyboard_port_data);
		if (tmp != ferro_ps2_keyboard_self_test_passed && tmp != ferro_ps2_keyboard_acknowledgement) {
			fconsole_logf("ps2-keyboard: keyboard self-test failed (second byte = %02x)\n", tmp);
			continue;
		}

		test_ok = true;
		break;
	}

	if (!test_ok) {
		fconsole_logf("ps2-keyboard: keyboard self-test attempts exhausted\n");
		return;
	} else {
		fconsole_log("ps2-keyboard: keyboard self-test passed\n");
	}

	command_ok = false;
	for (size_t i = 0; i < FERRO_PS2_KEYBOARD_MAX_RETRIES; ++i) {
		ferr_t status;

		ferro_ps2_keyboard_clear_data();

		// re-read the config
		if (ferro_ps2_keyboard_perform_command(ferro_ps2_keyboard_command_read_ram_byte_0, false, 0, true, &config) != ferr_ok) {
			fconsole_log("ps2-keyboard: failed to read config (2)\n");
			return;
		}

		if (config == ferro_ps2_keyboard_resend) {
			fconsole_logf("ps2-keyboard: read config retry (byte read was %02x)\n", config);
			continue;
		}

		ferro_ps2_keyboard_clear_data();

		fconsole_logf("ps2-keyboard: read config = 0x%02x\n", config);

		if ((config & ferro_ps2_keyboard_config_bit_first_port_interrupt_enabled) != 0 && (config & (ferro_ps2_keyboard_config_bit_first_port_clock_disabled | ferro_ps2_keyboard_config_bit_first_port_translation)) == 0) {
			command_ok = true;
			break;
		}

		// enable interrupts for the first port
		config |= ferro_ps2_keyboard_config_bit_first_port_interrupt_enabled;

		// enable the first port and disable translation
		config &= ~(ferro_ps2_keyboard_config_bit_first_port_clock_disabled | ferro_ps2_keyboard_config_bit_first_port_translation);

		fconsole_logf("ps2-keyboard: writing config = 0x%02x\n", config);

		// write the config
		if (ferro_ps2_keyboard_perform_command(ferro_ps2_keyboard_command_write_ram_byte_0, true, config, false, NULL) != ferr_ok) {
			fconsole_log("ps2-keyboard: failed to write config (3)\n");
			continue;
		}
	}

	if (!command_ok) {
		fconsole_logf("ps2-keyboard: keyboard config attempts exhausted\n");
		return;
	} else {
		fconsole_log("ps2-keyboard: keyboard config successfully set\n");
	}

	ferro_ps2_keyboard_clear_data();

	// set scan code set to 2
	command_ok = false;
	for (size_t i = 0; i < FERRO_PS2_KEYBOARD_MAX_RETRIES; ++i) {
		ferr_t status;

		ferro_ps2_keyboard_clear_data();

		status = ferro_ps2_keyboard_wait_for_write();
		if (status != ferr_ok) {
			fconsole_logf("ps2-keyboard: failed to send set-scan-code-set command (command write: %d: %s)\n", status, ferr_name(status));
			continue;
		}
		farch_lio_write_u8(ferro_ps2_keyboard_port_data, ferro_ps2_keyboard_command_get_or_set_scan_code_set);

		status = ferro_ps2_keyboard_wait_for_read();
		if (status != ferr_ok) {
			fconsole_logf("ps2-keyboard: failed to send set-scan-code-set command (command ack: %d: %s)\n", status, ferr_name(status));
			continue;
		}
		tmp = farch_lio_read_u8(ferro_ps2_keyboard_port_data);
		if (tmp != ferro_ps2_keyboard_acknowledgement) {
			fconsole_logf("ps2-keyboard: keyboard set-scan-code-set command failed (%02x)\n", tmp);
			continue;
		}

		status = ferro_ps2_keyboard_wait_for_write();
		if (status != ferr_ok) {
			fconsole_logf("ps2-keyboard: failed to send set-scan-code-set command (data write: %d: %s)\n", status, ferr_name(status));
			continue;
		}
		farch_lio_write_u8(ferro_ps2_keyboard_port_data, 2);

		status = ferro_ps2_keyboard_wait_for_read();
		if (status != ferr_ok) {
			fconsole_logf("ps2-keyboard: failed to send set-scan-code-set command (data ack %d: %s)\n", status, ferr_name(status));
			continue;
		}
		tmp = farch_lio_read_u8(ferro_ps2_keyboard_port_data);
		if (tmp != ferro_ps2_keyboard_acknowledgement) {
			fconsole_logf("ps2-keyboard: keyboard set-scan-code-set command failed (%02x)\n", tmp);
			continue;
		}

		command_ok = true;
		break;
	}

	ferro_ps2_keyboard_clear_data();

	if (!command_ok) {
		fconsole_log("ps2-keyboard: keyboard set-scan-code-set command attempts exhausted\n");
		return;
	} else {
		fconsole_log("ps2-keyboard: keyboard set-scan-code-set command successful\n");
	}

	if (farch_ioapic_unmask_legacy(1) != ferr_ok) {
		fconsole_log("ps2-keyboard: failed to unmask legacy IRQ #1\n");
		return;
	}
};
