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

#include <ferro/drivers/keyboard.h>
#include <libsimple/libsimple.h>
#include <ferro/core/console.h>

void fkeyboard_update_init(fkeyboard_state_t* state) {
	simple_memset(state, 0, sizeof(*state));
};

void fkeyboard_update_add(fkeyboard_state_t* state, fkeyboard_key_t key) {
	if (key >= fkeyboard_key_xxx_max) {
		return;
	}

	state->bitmap[key / 8] |= 1 << (key % 8);
};

void fkeyboard_update_remove(fkeyboard_state_t* state, fkeyboard_key_t key) {
	if (key >= fkeyboard_key_xxx_max) {
		return;
	}

	state->bitmap[key / 8] &= ~(1 << (key % 8));
};

void fkeyboard_update(const fkeyboard_state_t* state) {
	fconsole_logf(
		"keyboard: updated with: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
		state->bitmap[0],
		state->bitmap[1],
		state->bitmap[2],
		state->bitmap[3],
		state->bitmap[4],
		state->bitmap[5],
		state->bitmap[6],
		state->bitmap[7],
		state->bitmap[8],
		state->bitmap[9],
		state->bitmap[10],
		state->bitmap[11],
		state->bitmap[12],
		state->bitmap[13]
	);

	// TODO
};
