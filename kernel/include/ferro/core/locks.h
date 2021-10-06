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
 * Locks subsystem.
 */

#ifndef _FERRO_CORE_LOCKS_H_
#define _FERRO_CORE_LOCKS_H_

#include <stdbool.h>

#include <ferro/base.h>
#include <ferro/platform.h>
#include <ferro/error.h>

#include <ferro/core/locks.spin.h>

// include the arch-dependent before-header
#if FERRO_ARCH == FERRO_ARCH_x86_64
	#include <ferro/core/x86_64/locks.before.h>
#elif FERRO_ARCH == FERRO_ARCH_aarch64
	#include <ferro/core/aarch64/locks.before.h>
#else
	#error Unrecognized/unsupported CPU architecture! (see <ferro/core/locks.h>)
#endif

FERRO_DECLARATIONS_BEGIN;

/**
 * @addtogroup Locks
 *
 * The locks subsystem.
 *
 * @{
 */

//
// flock_semaphore_t
//

/*
 * A general-purpose semaphore.
 *
 * @note Semaphores *can* be used in both thread and interrupt contexts, but it is recommended NOT to use them in interrupt contexts because interrupt contexts run with interrupts disabled by default (unless explicitly re-enabled by the interrupt handler).
 *       The same warning applies to running in *any* context with interrupts disabled: if the code is running on a uniprocessor system and the semaphore needs to block while interrupts are disabled, the system will completely freeze.
 */
//typedef <something> flock_semaphore_t;

/**
 * Initializes an ::flock_semaphore at runtime.
 *
 * @param initial_count The initial up-count to assign to the semaphore.
 */
void flock_semaphore_init(flock_semaphore_t* semaphore, uint64_t initial_count);

/**
 * Increases the up-count of the given semaphore.
 *
 * @param semaphore The semaphore to operate on.
 *
 * @returns `true` if someone was woken up (i.e. the previous count was 0 and is now 1), or `false` otherwise.
 */
bool flock_semaphore_up(flock_semaphore_t* semaphore);

/**
 * Decreases the up-count of the given semaphore.
 *
 * @param semaphore The semaphore to operate on.
 *
 * @note If the semaphore's up-count before this operation was 0, this function will wait until it is increased by someone else.
 *       If running in a thread context, it will suspend the current thread until resumed by someone else increasing the up-count of the semaphore.
 *       If running in an interrupt context, it will spin-wait until someone else increases the up-count of the semaphore.
 */
void flock_semaphore_down(flock_semaphore_t* semaphore);

/**
 * Like flock_semaphore_down(), but never blocks.
 *
 * @param semaphore The semaphore to operate on.
 *
 * Return values:
 * @retval ferr_ok               The up-count was successfully decremented.
 * @retval ferr_temporary_outage The up-count was 0; decrementing it would require blocking.
 */
FERRO_WUR ferr_t flock_semaphore_try_down(flock_semaphore_t* semaphore);

//
// flock_mutex_t
//

/*
 * A general-purpose mutex.
 *
 * @note Like semaphores, mutexes *can* be used in both thread and interrupt contexts, but it is recommended NOT to use them in interrupt contexts because interrupt contexts run with interrupts disabled by default (unless explicitly re-enabled by the interrupt handler).
 *       The same warning applies to running in *any* context with interrupts disabled: if the code is running on a uniprocessor system and the mutex needs to block while interrupts are disabled, the system will completely freeze.
 *
 * @note Mutexes are always recursive; it is always safe to lock a mutex that you have already previously locked.
 */
//typedef <something> flock_mutex_t;

/*
 * A value that can be used to statically initialize an ::flock_mutex at compile-time.
 */
//#define FLOCK_MUTEX_INIT <something>

/**
 * Initializes an ::flock_semaphore at runtime.
 *
 * @param initial_count The initial up-count to assign to the semaphore.
 */
void flock_mutex_init(flock_mutex_t* mutex);

/**
 * Locks the given mutex.
 *
 * @param mutex The mutex to operate on.
 *
 * @note If the mutex was already locked by the calling thread, this function will return immediately. Otherwise, it will block until it is able to acquire the lock.
 *       If running in a thread context, it will suspend the current thread until resumed by someone else unlocking the mutex.
 *       If running in an interrupt context, it will spin-wait until someone else unlocks the mutex.
 */
void flock_mutex_lock(flock_mutex_t* mutex);

/**
 * Tries to lock the given mutex.
 *
 * @param mutex The mutex to operate on.
 *
 * @retval ferr_ok               The mutex was successfully locked.
 * @retval ferr_temporary_outage The mutex was already locked by someone else; locking it would require blocking.
 */
FERRO_WUR ferr_t flock_mutex_try_lock(flock_mutex_t* mutex);

/**
 * Unlocks the given mutex.
 *
 * @param mutex The mutex to operate on.
 *
 * @note Mutexes must only be unlocked by the thread that locked them. It is an error for anyone else to unlock it.
 */
void flock_mutex_unlock(flock_mutex_t* mutex);

/**
 * @}
 */

FERRO_DECLARATIONS_END;

// include the arch-dependent after-header
#if FERRO_ARCH == FERRO_ARCH_x86_64
	#include <ferro/core/x86_64/locks.after.h>
#elif FERRO_ARCH == FERRO_ARCH_aarch64
	#include <ferro/core/aarch64/locks.after.h>
#else
	#error Unrecognized/unsupported CPU architecture! (see <ferro/core/locks.h>)
#endif

#endif // _FERRO_CORE_LOCKS_H_