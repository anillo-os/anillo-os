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

#ifndef _FERRO_DRIVERS_KEYBOARD_H_
#define _FERRO_DRIVERS_KEYBOARD_H_

#include <ferro/base.h>
#include <ferro/error.h>

#include <stdint.h>
#include <stddef.h>

FERRO_DECLARATIONS_BEGIN;

FERRO_ENUM(uint8_t, fkeyboard_key) {
	fkeyboard_key_invalid = 0,

	fkeyboard_key_left_control,
	fkeyboard_key_left_shift,
	fkeyboard_key_left_alt,
	fkeyboard_key_left_meta,
	fkeyboard_key_right_control,
	fkeyboard_key_right_shift,
	fkeyboard_key_right_alt,
	fkeyboard_key_right_meta,

	fkeyboard_key_letter_a,
	fkeyboard_key_letter_b,
	fkeyboard_key_letter_c,
	fkeyboard_key_letter_d,
	fkeyboard_key_letter_e,
	fkeyboard_key_letter_f,
	fkeyboard_key_letter_g,
	fkeyboard_key_letter_h,
	fkeyboard_key_letter_i,
	fkeyboard_key_letter_j,
	fkeyboard_key_letter_k,
	fkeyboard_key_letter_l,
	fkeyboard_key_letter_m,
	fkeyboard_key_letter_n,
	fkeyboard_key_letter_o,
	fkeyboard_key_letter_p,
	fkeyboard_key_letter_q,
	fkeyboard_key_letter_r,
	fkeyboard_key_letter_s,
	// we have to use "fkeyboard_key_letter_t" because "fkeyboard_key_t" is the name of the type.
	// for consistency, the rest of the letters are also prefixed with "letter_".
	fkeyboard_key_letter_t,
	fkeyboard_key_letter_u,
	fkeyboard_key_letter_v,
	fkeyboard_key_letter_w,
	fkeyboard_key_letter_x,
	fkeyboard_key_letter_y,
	fkeyboard_key_letter_z,
	fkeyboard_key_1, // and !
	fkeyboard_key_2, // and @
	fkeyboard_key_3, // and #
	fkeyboard_key_4, // and $
	fkeyboard_key_5, // and %
	fkeyboard_key_6, // and ^
	fkeyboard_key_7, // and &
	fkeyboard_key_8, // and *
	fkeyboard_key_9, // and (
	fkeyboard_key_0, // and )
	fkeyboard_key_return,
	fkeyboard_key_escape,
	fkeyboard_key_backspace,
	fkeyboard_key_tab,
	fkeyboard_key_space,
	fkeyboard_key_minus, // and underscore
	fkeyboard_key_equals, // and plus
	fkeyboard_key_opening_bracket, // "[" and opening brace "{"
	fkeyboard_key_closing_bracket, // "]" and closing brace "}"
	fkeyboard_key_backslash, // "\" and pipe "|"
	fkeyboard_key_semicolon, // ";" and colon ":"
	fkeyboard_key_apostrophe, // "'" and quotation mark """
	fkeyboard_key_grave_accent, // "`" and tilde "~"
	fkeyboard_key_comma, // "," and left angle bracket "<"
	fkeyboard_key_dot, // "." and right angle bracket ">"
	fkeyboard_key_slash, // "/" and question mark "?"
	fkeyboard_key_caps_lock,
	fkeyboard_key_f1,
	fkeyboard_key_f2,
	fkeyboard_key_f3,
	fkeyboard_key_f4,
	fkeyboard_key_f5,
	fkeyboard_key_f6,
	fkeyboard_key_f7,
	fkeyboard_key_f8,
	fkeyboard_key_f9,
	fkeyboard_key_f10,
	fkeyboard_key_f11,
	fkeyboard_key_f12,
	fkeyboard_key_print_screen,
	fkeyboard_key_scroll_lock,
	fkeyboard_key_pause,
	fkeyboard_key_insert,
	fkeyboard_key_home,
	fkeyboard_key_page_up,
	fkeyboard_key_delete,
	fkeyboard_key_end,
	fkeyboard_key_page_down,
	fkeyboard_key_right_arrow,
	fkeyboard_key_left_arrow,
	fkeyboard_key_down_arrow,
	fkeyboard_key_up_arrow,
	fkeyboard_key_num_lock,
	fkeyboard_key_keypad_divide,
	fkeyboard_key_keypad_times,
	fkeyboard_key_keypad_minus,
	fkeyboard_key_keypad_plus,
	fkeyboard_key_keypad_enter,
	fkeyboard_key_keypad_1, // and end
	fkeyboard_key_keypad_2, // and down arrow
	fkeyboard_key_keypad_3, // and page down
	fkeyboard_key_keypad_4, // and left arrow
	fkeyboard_key_keypad_5,
	fkeyboard_key_keypad_6, // and right arrow
	fkeyboard_key_keypad_7, // and home
	fkeyboard_key_keypad_8, // and up arrow
	fkeyboard_key_keypad_9, // and page up
	fkeyboard_key_keypad_0, // and insert
	fkeyboard_key_keypad_dot, // and delete
	fkeyboard_key_application,

	fkeyboard_key_xxx_max,
};

FERRO_STRUCT(fkeyboard_state) {
	uint8_t bitmap[(fkeyboard_key_xxx_max + 7) / 8];
};

void fkeyboard_update_init(fkeyboard_state_t* state);
void fkeyboard_update_add(fkeyboard_state_t* state, fkeyboard_key_t key);
void fkeyboard_update_remove(fkeyboard_state_t* state, fkeyboard_key_t key);

void fkeyboard_update(const fkeyboard_state_t* state);

FERRO_DECLARATIONS_END;

#endif // _FERRO_DRIVERS_KEYBOARD_H_
