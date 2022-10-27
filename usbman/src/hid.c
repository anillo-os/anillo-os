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

#include <usbman/hid.private.h>
#include <usbman/usb.private.h>
#include <libsimple/libsimple.h>
#include <ferro/bits.h>

static ferr_t usbman_hid_interface_class_process_descriptor(usbman_interface_setting_t* interface_setting, const usbman_descriptor_header_t* descriptor, void** in_out_private_data) {
	// TODO
	return ferr_invalid_argument;
};

static void usbman_hid_interface_class_free_context(void* private_data) {
	// TODO
};

USBMAN_ALWAYS_INLINE uint8_t round_down_to_alignment_power(uint64_t byte_count) {
	if (byte_count == 0) {
		return 0;
	}
	return ferro_bits_in_use_u64(byte_count) - 1;
};

USBMAN_ALWAYS_INLINE uint8_t round_up_to_alignment_power(uint64_t byte_count) {
	uint8_t power = round_down_to_alignment_power(byte_count);
	return ((1ull << power) < byte_count) ? (power + 1) : power;
};

static void usbman_hid_keyboard_polling_thread(void* context, sys_thread_t* this_thread) {
	usbman_interface_t* interface = context;
	uint8_t* buffer = NULL;
	ferr_t status = ferr_ok;
	void* physical_buffer = NULL;

	status = sys_mempool_allocate_advanced(8, 0, round_up_to_alignment_power(64 * 1024), sys_mempool_flag_physically_contiguous, NULL, (void*)&buffer);
	if (status != ferr_ok) {
		goto out;
	}

	status = sys_page_translate(buffer, (void*)&physical_buffer);
	if (status != ferr_ok) {
		goto out;
	}

	while (true) {
		uint16_t transferred = 0;
#if 0
		fkeyboard_state_t state;
#endif

		simple_memset(buffer, 0, 8);

		status = usbman_endpoint_perform_transfer_blocking(interface->active_setting->endpoints[0], physical_buffer, 8, &transferred);
		if (status != ferr_ok) {
			sys_console_log("USB-HID: failed to transfer data from keyboard\n");
			continue;
		}

		if (transferred < 8) {
			sys_console_log("USB-HID: transferred less than expected\n");
			continue;
		}

		if (buffer[2] == usbman_hid_keyboard_keycode_overflow && buffer[2] == buffer[3] && buffer[3] == buffer[4] && buffer[4] == buffer[5] && buffer[5] == buffer[6] && buffer[6] == buffer[7]) {
			// overflow/phantom key;
			// ignore this update
			continue;
		}

#if 0
		fkeyboard_update_init(&state);

		if (buffer[0] & usbman_hid_keyboard_modifier_left_control) {
			fkeyboard_update_add(&state, fkeyboard_key_left_control);
		}

		if (buffer[0] & usbman_hid_keyboard_modifier_left_shift) {
			fkeyboard_update_add(&state, fkeyboard_key_left_shift);
		}

		if (buffer[0] & usbman_hid_keyboard_modifier_left_alt) {
			fkeyboard_update_add(&state, fkeyboard_key_left_alt);
		}

		if (buffer[0] & usbman_hid_keyboard_modifier_left_meta) {
			fkeyboard_update_add(&state, fkeyboard_key_left_meta);
		}

		if (buffer[0] & usbman_hid_keyboard_modifier_right_control) {
			fkeyboard_update_add(&state, fkeyboard_key_right_control);
		}

		if (buffer[0] & usbman_hid_keyboard_modifier_right_shift) {
			fkeyboard_update_add(&state, fkeyboard_key_right_shift);
		}

		if (buffer[0] & usbman_hid_keyboard_modifier_right_alt) {
			fkeyboard_update_add(&state, fkeyboard_key_right_alt);
		}

		if (buffer[0] & usbman_hid_keyboard_modifier_right_meta) {
			fkeyboard_update_add(&state, fkeyboard_key_right_meta);
		}

		for (uint8_t i = 2; i < 8; ++i) {
			usbman_hid_keyboard_keycode_t keycode = buffer[i];

			if (keycode >= usbman_hid_keyboard_keycode_letter_a && keycode <= usbman_hid_keyboard_keycode_letter_z) {
				fkeyboard_update_add(&state, fkeyboard_key_letter_a + (keycode - usbman_hid_keyboard_keycode_letter_a));
			} else if (keycode >= usbman_hid_keyboard_keycode_1 && keycode <= usbman_hid_keyboard_keycode_0) {
				fkeyboard_update_add(&state, fkeyboard_key_1 + (keycode - usbman_hid_keyboard_keycode_1));
			} else if (keycode >= usbman_hid_keyboard_keycode_return && keycode <= usbman_hid_keyboard_keycode_backslash) {
				fkeyboard_update_add(&state, fkeyboard_key_return + (keycode - usbman_hid_keyboard_keycode_return));
			} else if (keycode >= usbman_hid_keyboard_keycode_semicolon && keycode <= usbman_hid_keyboard_keycode_keypad_dot) {
				fkeyboard_update_add(&state, fkeyboard_key_semicolon + (keycode - usbman_hid_keyboard_keycode_semicolon));
			} else if (keycode == usbman_hid_keyboard_keycode_application) {
				fkeyboard_update_add(&state, fkeyboard_key_application);
			}
		}

		fkeyboard_update(&state);
#endif

		sys_console_log_f("USB-HID: keyboard (%u bytes): %02x %02x %02x %02x %02x %02x %02x %02x\n", transferred, buffer[0], buffer[1], buffer[2], buffer[3], buffer[4], buffer[5], buffer[6], buffer[7]);
	}

out:
	return;
};

static void usbman_hid_mouse_polling_thread(void* context, sys_thread_t* this_thread) {
	usbman_interface_t* interface = context;
	uint8_t* buffer = NULL;
	ferr_t status = ferr_ok;
	void* physical_buffer = NULL;

	status = sys_mempool_allocate_advanced(8, 0, round_up_to_alignment_power(64 * 1024), sys_mempool_flag_physically_contiguous, NULL, (void*)&buffer);
	if (status != ferr_ok) {
		goto out;
	}

	status = sys_page_translate(buffer, (void*)&physical_buffer);
	if (status != ferr_ok) {
		goto out;
	}

	while (true) {
		uint16_t transferred = 0;
#if 0
		fmouse_button_t buttons = 0;
#endif

		simple_memset(buffer, 0, 8);

		status = usbman_endpoint_perform_transfer_blocking(interface->active_setting->endpoints[0], physical_buffer, 8, &transferred);
		if (status != ferr_ok) {
			sys_console_log("USB-HID: failed to transfer data from mouse\n");
			continue;
		}

		if (transferred < 3) {
			sys_console_log("USB-HID: transferred less than expected\n");
			continue;
		}

#if 0
		if ((buffer[0] & (1 << 0)) != 0) {
			buttons |= fmouse_button_left;
		}

		if ((buffer[0] & (1 << 1)) != 0) {
			buttons |= fmouse_button_right;
		}

		if ((buffer[0] & (1 << 2)) != 0) {
			buttons |= fmouse_button_middle;
		}

		fmouse_update(buttons, (int8_t)buffer[1], -(int8_t)buffer[2], (transferred > 3) ? (int8_t)buffer[3] : 0);
#endif

		sys_console_log_f("USB-HID: mouse (%u bytes): %02x %02x %02x %02x %02x %02x %02x %02x\n", transferred, buffer[0], buffer[1], buffer[2], buffer[3], buffer[4], buffer[5], buffer[6], buffer[7]);
	}

out:
	return;
};

static void usbman_hid_interface_class_setup_interface(usbman_interface_t* interface) {
	// we MUST have at least an interrupt in pipe
	fassert(interface->active_setting->endpoint_count > 0);

	if (interface->active_setting->interface_subclass == 1) {
		// boot protocol interface

		// switch to the boot protocol
		if (usbman_device_make_request_blocking(interface->configuration->device, usbman_request_direction_host_to_device, usbman_request_type_class, usbman_request_recipient_interface, 0x0b, 0, interface->id, NULL, 0) != ferr_ok) {
			sys_console_log("USB-HID: failed to switch device to boot protocol\n");
			goto boot2_out;
		}

		if (interface->active_setting->interface_protocol == 1 || interface->active_setting->interface_protocol == 2) {
			bool managed = false;
			ferr_t status = ferr_ok;
			bool is_keyboard = interface->active_setting->interface_protocol == 1;
			sys_thread_entry_f init = is_keyboard ? usbman_hid_keyboard_polling_thread : usbman_hid_mouse_polling_thread;

			status = sys_thread_create(NULL, 2ull * 1024 * 1024, init, interface, sys_thread_flag_resume, NULL);
			if (status != ferr_ok) {
				sys_console_log_f("USB-HID: failed to start polling thread for %s\n", is_keyboard ? "keyboard" : "mouse");
			}
		}

boot2_out:
		;
	}
};

static const usbman_interface_class_methods_t methods = {
	.process_descriptor = usbman_hid_interface_class_process_descriptor,
	.free_context = usbman_hid_interface_class_free_context,
	.setup_interface = usbman_hid_interface_class_setup_interface,
};

void usbman_hid_init(void) {
	sys_abort_status_log(usbman_register_interface_class(0x03, &methods));
};
