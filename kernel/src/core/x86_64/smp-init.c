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

#include <ferro/core/x86_64/smp-init.h>

// TEMPORARY
#include <ferro/core/entry.h>

// interrupts are disabled on entry
FERRO_NO_RETURN
void farch_smp_init_entry(farch_smp_init_data_t* init_data) {
	__atomic_store_n(&init_data->init_done, 1, __ATOMIC_RELAXED);

	while (true) {
		// do nothing
		fentry_idle();
	}
};
