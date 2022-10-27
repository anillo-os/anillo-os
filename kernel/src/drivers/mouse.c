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

#include <ferro/drivers/mouse.h>
#include <libsimple/libsimple.h>
#include <ferro/core/console.h>

void fmouse_update(fmouse_button_t buttons, int64_t delta_x, int64_t delta_y, int64_t delta_scroll) {
	fmouse_state_t mouse_state;

	simple_memset(&mouse_state, 0, sizeof(mouse_state));

	mouse_state.buttons = buttons;
	mouse_state.delta_x = delta_x;
	mouse_state.delta_y = delta_y;
	mouse_state.delta_scroll = delta_scroll;

	fconsole_logf(
		"mouse: updated with: buttons=(left=%s, right=%s, middle=%s), delta_x=%lld, delta_y=%lld, delta_scroll=%lld\n",
		(mouse_state.buttons & fmouse_button_left) != 0 ? "1" : "0",
		(mouse_state.buttons & fmouse_button_right) != 0 ? "1" : "0",
		(mouse_state.buttons & fmouse_button_middle) != 0 ? "1" : "0",
		mouse_state.delta_x,
		mouse_state.delta_y,
		mouse_state.delta_scroll
	);

	// TODO
};
