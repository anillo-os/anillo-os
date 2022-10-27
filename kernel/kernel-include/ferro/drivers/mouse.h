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

#ifndef _FERRO_DRIVERS_MOUSE_H_
#define _FERRO_DRIVERS_MOUSE_H_

#include <ferro/base.h>
#include <ferro/error.h>

#include <stdint.h>
#include <stddef.h>

FERRO_DECLARATIONS_BEGIN;

FERRO_ENUM(uint8_t, fmouse_button) {
	fmouse_button_left   = 1 << 0,
	fmouse_button_right  = 1 << 1,
	fmouse_button_middle = 1 << 2,
};

FERRO_STRUCT(fmouse_state) {
	fmouse_button_t buttons;
	int64_t delta_x;
	int64_t delta_y;
	int64_t delta_scroll;
};

/**
 * @param buttons      Bitmap of buttons currently pressed on the mouse.
 * @param delta_x      Change in X; see notes.
 * @param delta_y      Change in Y; see notes.
 * @param delta_scroll Change in scroll wheel; see notes
 *
 * @note The position delta values are as if the screen were a coordinate plane,
 *       i.e. going left is negative X, going right is positive X,
 *       going down is negative Y, and going up is positive Y.
 *
 * @note The scroll delta value is positive if the scroll wheel was rolled away from the user
 *       and negative if it was rolled towards the user.
 */
void fmouse_update(fmouse_button_t buttons, int64_t delta_x, int64_t delta_y, int64_t delta_scroll);

FERRO_DECLARATIONS_END;

#endif // _FERRO_DRIVERS_MOUSE_H_
