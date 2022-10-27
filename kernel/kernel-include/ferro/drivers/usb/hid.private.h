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

#ifndef _FERRO_DRIVERS_USB_HID_PRIVATE_H_
#define _FERRO_DRIVERS_USB_HID_PRIVATE_H_

#include <ferro/drivers/usb/hid.h>

FERRO_DECLARATIONS_BEGIN;

FERRO_ENUM(uint8_t, fusb_hid_keyboard_keycode) {
	fusb_hid_keyboard_keycode_none = 0,
	fusb_hid_keyboard_keycode_overflow,

	fusb_hid_keyboard_keycode_letter_a = 0x04,
	fusb_hid_keyboard_keycode_letter_b,
	fusb_hid_keyboard_keycode_letter_c,
	fusb_hid_keyboard_keycode_letter_d,
	fusb_hid_keyboard_keycode_letter_e,
	fusb_hid_keyboard_keycode_letter_f,
	fusb_hid_keyboard_keycode_letter_g,
	fusb_hid_keyboard_keycode_letter_h,
	fusb_hid_keyboard_keycode_letter_i,
	fusb_hid_keyboard_keycode_letter_j,
	fusb_hid_keyboard_keycode_letter_k,
	fusb_hid_keyboard_keycode_letter_l,
	fusb_hid_keyboard_keycode_letter_m,
	fusb_hid_keyboard_keycode_letter_n,
	fusb_hid_keyboard_keycode_letter_o,
	fusb_hid_keyboard_keycode_letter_p,
	fusb_hid_keyboard_keycode_letter_q,
	fusb_hid_keyboard_keycode_letter_r,
	fusb_hid_keyboard_keycode_letter_s,
	fusb_hid_keyboard_keycode_letter_t,
	fusb_hid_keyboard_keycode_letter_u,
	fusb_hid_keyboard_keycode_letter_v,
	fusb_hid_keyboard_keycode_letter_w,
	fusb_hid_keyboard_keycode_letter_x,
	fusb_hid_keyboard_keycode_letter_y,
	fusb_hid_keyboard_keycode_letter_z,
	fusb_hid_keyboard_keycode_1,
	fusb_hid_keyboard_keycode_2,
	fusb_hid_keyboard_keycode_3,
	fusb_hid_keyboard_keycode_4,
	fusb_hid_keyboard_keycode_5,
	fusb_hid_keyboard_keycode_6,
	fusb_hid_keyboard_keycode_7,
	fusb_hid_keyboard_keycode_8,
	fusb_hid_keyboard_keycode_9,
	fusb_hid_keyboard_keycode_0,
	fusb_hid_keyboard_keycode_return,
	fusb_hid_keyboard_keycode_escape,
	fusb_hid_keyboard_keycode_backspace,
	fusb_hid_keyboard_keycode_tab,
	fusb_hid_keyboard_keycode_space,
	fusb_hid_keyboard_keycode_minus,
	fusb_hid_keyboard_keycode_equals,
	fusb_hid_keyboard_keycode_opening_bracket,
	fusb_hid_keyboard_keycode_closing_bracket,
	fusb_hid_keyboard_keycode_backslash,

	fusb_hid_keyboard_keycode_semicolon = 0x33,
	fusb_hid_keyboard_keycode_apostrophe,
	fusb_hid_keyboard_keycode_grave_accent,
	fusb_hid_keyboard_keycode_comma,
	fusb_hid_keyboard_keycode_dot,
	fusb_hid_keyboard_keycode_slash,
	fusb_hid_keyboard_keycode_caps_lock,
	fusb_hid_keyboard_keycode_f1,
	fusb_hid_keyboard_keycode_f2,
	fusb_hid_keyboard_keycode_f3,
	fusb_hid_keyboard_keycode_f4,
	fusb_hid_keyboard_keycode_f5,
	fusb_hid_keyboard_keycode_f6,
	fusb_hid_keyboard_keycode_f7,
	fusb_hid_keyboard_keycode_f8,
	fusb_hid_keyboard_keycode_f9,
	fusb_hid_keyboard_keycode_f10,
	fusb_hid_keyboard_keycode_f11,
	fusb_hid_keyboard_keycode_f12,
	fusb_hid_keyboard_keycode_print_screen,
	fusb_hid_keyboard_keycode_scroll_lock,
	fusb_hid_keyboard_keycode_pause,
	fusb_hid_keyboard_keycode_insert,
	fusb_hid_keyboard_keycode_home,
	fusb_hid_keyboard_keycode_page_up,
	fusb_hid_keyboard_keycode_delete,
	fusb_hid_keyboard_keycode_end,
	fusb_hid_keyboard_keycode_page_down,
	fusb_hid_keyboard_keycode_right,
	fusb_hid_keyboard_keycode_left,
	fusb_hid_keyboard_keycode_down,
	fusb_hid_keyboard_keycode_up,
	fusb_hid_keyboard_keycode_num_lock,
	fusb_hid_keyboard_keycode_keypad_divide,
	fusb_hid_keyboard_keycode_keypad_times,
	fusb_hid_keyboard_keycode_keypad_minus,
	fusb_hid_keyboard_keycode_keypad_plus,
	fusb_hid_keyboard_keycode_keypad_enter,
	fusb_hid_keyboard_keycode_keypad_1,
	fusb_hid_keyboard_keycode_keypad_2,
	fusb_hid_keyboard_keycode_keypad_3,
	fusb_hid_keyboard_keycode_keypad_4,
	fusb_hid_keyboard_keycode_keypad_5,
	fusb_hid_keyboard_keycode_keypad_6,
	fusb_hid_keyboard_keycode_keypad_7,
	fusb_hid_keyboard_keycode_keypad_8,
	fusb_hid_keyboard_keycode_keypad_9,
	fusb_hid_keyboard_keycode_keypad_0,
	fusb_hid_keyboard_keycode_keypad_dot,

	fusb_hid_keyboard_keycode_application = 0x65,
};

FERRO_ENUM(uint8_t, fusb_hid_keyboard_modifier) {
	fusb_hid_keyboard_modifier_left_control  = 1 << 0,
	fusb_hid_keyboard_modifier_left_shift    = 1 << 1,
	fusb_hid_keyboard_modifier_left_alt      = 1 << 2,
	fusb_hid_keyboard_modifier_left_meta     = 1 << 3,
	fusb_hid_keyboard_modifier_right_control = 1 << 4,
	fusb_hid_keyboard_modifier_right_shift   = 1 << 5,
	fusb_hid_keyboard_modifier_right_alt     = 1 << 6,
	fusb_hid_keyboard_modifier_right_meta    = 1 << 7,
};

FERRO_DECLARATIONS_END;

#endif // _FERRO_DRIVERS_USB_HID_PRIVATE_H_
