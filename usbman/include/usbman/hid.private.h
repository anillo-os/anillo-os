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

#ifndef _USBMAN_HID_PRIVATE_H_
#define _USBMAN_HID_PRIVATE_H_

#include <usbman/hid.h>

USBMAN_DECLARATIONS_BEGIN;

USBMAN_ENUM(uint8_t, usbman_hid_keyboard_keycode) {
	usbman_hid_keyboard_keycode_none = 0,
	usbman_hid_keyboard_keycode_overflow,

	usbman_hid_keyboard_keycode_letter_a = 0x04,
	usbman_hid_keyboard_keycode_letter_b,
	usbman_hid_keyboard_keycode_letter_c,
	usbman_hid_keyboard_keycode_letter_d,
	usbman_hid_keyboard_keycode_letter_e,
	usbman_hid_keyboard_keycode_letter_f,
	usbman_hid_keyboard_keycode_letter_g,
	usbman_hid_keyboard_keycode_letter_h,
	usbman_hid_keyboard_keycode_letter_i,
	usbman_hid_keyboard_keycode_letter_j,
	usbman_hid_keyboard_keycode_letter_k,
	usbman_hid_keyboard_keycode_letter_l,
	usbman_hid_keyboard_keycode_letter_m,
	usbman_hid_keyboard_keycode_letter_n,
	usbman_hid_keyboard_keycode_letter_o,
	usbman_hid_keyboard_keycode_letter_p,
	usbman_hid_keyboard_keycode_letter_q,
	usbman_hid_keyboard_keycode_letter_r,
	usbman_hid_keyboard_keycode_letter_s,
	usbman_hid_keyboard_keycode_letter_t,
	usbman_hid_keyboard_keycode_letter_u,
	usbman_hid_keyboard_keycode_letter_v,
	usbman_hid_keyboard_keycode_letter_w,
	usbman_hid_keyboard_keycode_letter_x,
	usbman_hid_keyboard_keycode_letter_y,
	usbman_hid_keyboard_keycode_letter_z,
	usbman_hid_keyboard_keycode_1,
	usbman_hid_keyboard_keycode_2,
	usbman_hid_keyboard_keycode_3,
	usbman_hid_keyboard_keycode_4,
	usbman_hid_keyboard_keycode_5,
	usbman_hid_keyboard_keycode_6,
	usbman_hid_keyboard_keycode_7,
	usbman_hid_keyboard_keycode_8,
	usbman_hid_keyboard_keycode_9,
	usbman_hid_keyboard_keycode_0,
	usbman_hid_keyboard_keycode_return,
	usbman_hid_keyboard_keycode_escape,
	usbman_hid_keyboard_keycode_backspace,
	usbman_hid_keyboard_keycode_tab,
	usbman_hid_keyboard_keycode_space,
	usbman_hid_keyboard_keycode_minus,
	usbman_hid_keyboard_keycode_equals,
	usbman_hid_keyboard_keycode_opening_bracket,
	usbman_hid_keyboard_keycode_closing_bracket,
	usbman_hid_keyboard_keycode_backslash,

	usbman_hid_keyboard_keycode_semicolon = 0x33,
	usbman_hid_keyboard_keycode_apostrophe,
	usbman_hid_keyboard_keycode_grave_accent,
	usbman_hid_keyboard_keycode_comma,
	usbman_hid_keyboard_keycode_dot,
	usbman_hid_keyboard_keycode_slash,
	usbman_hid_keyboard_keycode_caps_lock,
	usbman_hid_keyboard_keycode_f1,
	usbman_hid_keyboard_keycode_f2,
	usbman_hid_keyboard_keycode_f3,
	usbman_hid_keyboard_keycode_f4,
	usbman_hid_keyboard_keycode_f5,
	usbman_hid_keyboard_keycode_f6,
	usbman_hid_keyboard_keycode_f7,
	usbman_hid_keyboard_keycode_f8,
	usbman_hid_keyboard_keycode_f9,
	usbman_hid_keyboard_keycode_f10,
	usbman_hid_keyboard_keycode_f11,
	usbman_hid_keyboard_keycode_f12,
	usbman_hid_keyboard_keycode_print_screen,
	usbman_hid_keyboard_keycode_scroll_lock,
	usbman_hid_keyboard_keycode_pause,
	usbman_hid_keyboard_keycode_insert,
	usbman_hid_keyboard_keycode_home,
	usbman_hid_keyboard_keycode_page_up,
	usbman_hid_keyboard_keycode_delete,
	usbman_hid_keyboard_keycode_end,
	usbman_hid_keyboard_keycode_page_down,
	usbman_hid_keyboard_keycode_right,
	usbman_hid_keyboard_keycode_left,
	usbman_hid_keyboard_keycode_down,
	usbman_hid_keyboard_keycode_up,
	usbman_hid_keyboard_keycode_num_lock,
	usbman_hid_keyboard_keycode_keypad_divide,
	usbman_hid_keyboard_keycode_keypad_times,
	usbman_hid_keyboard_keycode_keypad_minus,
	usbman_hid_keyboard_keycode_keypad_plus,
	usbman_hid_keyboard_keycode_keypad_enter,
	usbman_hid_keyboard_keycode_keypad_1,
	usbman_hid_keyboard_keycode_keypad_2,
	usbman_hid_keyboard_keycode_keypad_3,
	usbman_hid_keyboard_keycode_keypad_4,
	usbman_hid_keyboard_keycode_keypad_5,
	usbman_hid_keyboard_keycode_keypad_6,
	usbman_hid_keyboard_keycode_keypad_7,
	usbman_hid_keyboard_keycode_keypad_8,
	usbman_hid_keyboard_keycode_keypad_9,
	usbman_hid_keyboard_keycode_keypad_0,
	usbman_hid_keyboard_keycode_keypad_dot,

	usbman_hid_keyboard_keycode_application = 0x65,
};

USBMAN_ENUM(uint8_t, usbman_hid_keyboard_modifier) {
	usbman_hid_keyboard_modifier_left_control  = 1 << 0,
	usbman_hid_keyboard_modifier_left_shift    = 1 << 1,
	usbman_hid_keyboard_modifier_left_alt      = 1 << 2,
	usbman_hid_keyboard_modifier_left_meta     = 1 << 3,
	usbman_hid_keyboard_modifier_right_control = 1 << 4,
	usbman_hid_keyboard_modifier_right_shift   = 1 << 5,
	usbman_hid_keyboard_modifier_right_alt     = 1 << 6,
	usbman_hid_keyboard_modifier_right_meta    = 1 << 7,
};

USBMAN_DECLARATIONS_END;

#endif // _USBMAN_HID_PRIVATE_H_
