/*
 * This file is part of Anillo OS
 * Copyright (C) 2023 Anillo OS Developers
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

#ifndef _LIBSYS_ONCE_PRIVATE_H_
#define _LIBSYS_ONCE_PRIVATE_H_

#include <libsys/once.h>

LIBSYS_DECLARATIONS_BEGIN;

LIBSYS_ENUM(uint64_t, sys_once_state) {
	sys_once_state_init = 0,
	sys_once_state_done = 1,
	sys_once_state_perform_no_wait = 2,
	sys_once_state_perform_wait = 3,
};

LIBSYS_DECLARATIONS_END;

#endif // _LIBSYS_ONCE_PRIVATE_H_
