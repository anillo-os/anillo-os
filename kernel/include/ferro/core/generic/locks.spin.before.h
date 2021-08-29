/*
 * This file is part of Anillo OS
 * Copyright (C) 2021 Anillo OS Developers
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

#ifndef _FERRO_CORE_GENERIC_LOCKS_SPIN_BEFORE_H_
#define _FERRO_CORE_GENERIC_LOCKS_SPIN_BEFORE_H_

#include <stdint.h>

#include <ferro/base.h>

FERRO_DECLARATIONS_BEGIN;

/**
 * A general-purpose spinlock.
 */
FERRO_STRUCT(flock_spin) {
	uint8_t flag;
};

/**
 * A value that can be used to statically initialize an `flock_spin_t` at compile-time.
 */
#define FLOCK_SPIN_INIT {0}

/**
 * A general-purpose spinlock that can also be locked in an interrupt-safe way.
 */
FERRO_STRUCT(flock_spin_intsafe) {
	flock_spin_t base;
};

/**
 * A value that can be used to statically initialize an `flock_spin_intsafe_t`.
 */
#define FLOCK_SPIN_INTSAFE_INIT {0}

FERRO_DECLARATIONS_END;

#include <ferro/core/locks.spin.h>

#endif // _FERRO_CORE_GENERIC_LOCKS_SPIN_BEFORE_H_
