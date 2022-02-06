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

#ifndef _LIBSYS_LOCKS_PRIVATE_H_
#define _LIBSYS_LOCKS_PRIVATE_H_

#include <libsys/locks.h>

LIBSYS_DECLARATIONS_BEGIN;

LIBSYS_ENUM(uint64_t, sys_mutex_state) {
	sys_mutex_state_unlocked = 0,
	sys_mutex_state_locked_uncontended = 1,
	sys_mutex_state_locked_contended = 2,
};

LIBSYS_ENUM(uint64_t, sys_semaphore_state) {
	sys_semaphore_state_up_needs_to_wake_bit = 1ULL << 63,
};

LIBSYS_ENUM(uint64_t, sys_event_state) {
	sys_event_state_unset_no_wait = 0,
	sys_event_state_unset_wait = 1,
	sys_event_state_set = 2,
};

LIBSYS_DECLARATIONS_END;

#endif // _LIBSYS_LOCKS_PRIVATE_H_
