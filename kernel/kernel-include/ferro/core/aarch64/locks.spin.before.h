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

/**
 * @file
 *
 * AARCH64 implementations of architecture-specific components for locks subsystem; spinlock component; before-header.
 */

#ifndef _FERRO_CORE_AARCH64_LOCKS_SPIN_BEFORE_H_
#define _FERRO_CORE_AARCH64_LOCKS_SPIN_BEFORE_H_

#include <ferro/base.h>

FERRO_DECLARATIONS_BEGIN;

/**
 * @addtogroup Locks
 *
 * @{
 */

FERRO_ALWAYS_INLINE void farch_lock_spin_yield(void) {
	__asm__ volatile("yield" :::);
};

/**
 * @}
 */

FERRO_DECLARATIONS_END;

#include <ferro/core/generic/locks.spin.before.h>

#endif // _FERRO_CORE_AARCH64_LOCKS_SPIN_BEFORE_H_
