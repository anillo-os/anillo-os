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
 * Generic implementations of architecture-specific components for locks subsystem; before-header.
 */

#ifndef _FERRO_CORE_GENERIC_LOCKS_BEFORE_H_
#define _FERRO_CORE_GENERIC_LOCKS_BEFORE_H_

#include <stdint.h>

#include <ferro/base.h>

#include <ferro/core/waitq.h>

FERRO_DECLARATIONS_BEGIN;

/**
 * @addtogroup Locks
 *
 * @{
 */

/**
 * A general-purpose semaphore.
 *
 * @note Semaphores *can* be used in both thread and interrupt contexts, but it is recommended NOT to use them in interrupt contexts because interrupt contexts run with interrupts disabled by default (unless explicitly re-enabled by the interrupt handler).
 *       The same warning applies to running in *any* context with interrupts disabled: if the code is running on a uniprocessor system and the semaphore needs to block while interrupts are disabled, the system will completely freeze.
 */
FERRO_STRUCT(flock_semaphore) {
	uint64_t up_count;
	fwaitq_t waitq;
};

/*
 * A general-purpose mutex.
 *
 * @note Like semaphores, mutexes *can* be used in both thread and interrupt contexts, but it is recommended NOT to use them in interrupt contexts because interrupt contexts run with interrupts disabled by default (unless explicitly re-enabled by the interrupt handler).
 *       The same warning applies to running in *any* context with interrupts disabled: if the code is running on a uniprocessor system and the mutex needs to block while interrupts are disabled, the system will completely freeze.
 *
 * @note Mutexes are always recursive; it is always safe to lock a mutex that you have already previously locked.
 */
FERRO_STRUCT(flock_mutex) {
	uint64_t owner;
	uint64_t lock_count;
	fwaitq_t waitq;
};

/*
 * A value that can be used to statically initialize an ::flock_mutex at compile-time.
 */
#define FLOCK_MUTEX_INIT { .owner = UINT64_MAX }

/**
 * @}
 */

FERRO_DECLARATIONS_END;

#include <ferro/core/locks.h>

#endif // _FERRO_CORE_GENERIC_LOCKS_BEFORE_H_
