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
 * Locks subsystem; spinlock component.
 *
 * There are subsystems that need spinlocks but which are depended on by the other kinds of locks.
 * This file is meant to avoid cyclic header dependencies in those cases.
 */

#ifndef _FERRO_CORE_LOCKS_SPIN_H_
#define _FERRO_CORE_LOCKS_SPIN_H_

#include <stdbool.h>

#include <ferro/base.h>
#include <ferro/platform.h>

// include the arch-dependent before-header
#if FERRO_ARCH == FERRO_ARCH_x86_64
	#include <ferro/core/x86_64/locks.spin.before.h>
#elif FERRO_ARCH == FERRO_ARCH_aarch64
	#include <ferro/core/aarch64/locks.spin.before.h>
#else
	#error Unrecognized/unsupported CPU architecture! (see <ferro/core/locks.h>)
#endif

FERRO_DECLARATIONS_BEGIN;

/**
 * @addtogroup Locks
 *
 * @{
 */

//
// flock_spin_t
//

/*
 * A general-purpose spinlock.
 */
//typedef <something> flock_spin_t;

/*
 * A value that can be used to statically initialize an ::flock_spin at compile-time.
 */
//#define FLOCK_SPIN_INIT <something>

/**
 * Initializes an ::flock_spin at runtime.
 */
void flock_spin_init(flock_spin_t* lock);

/**
 * Lock an ::flock_spin.
 *
 * This function will not return until it is acquired.
 */
void flock_spin_lock(flock_spin_t* lock);

/**
 * Try to lock an ::flock_spin.
 *
 * If the lock cannot be acquired immediately, this function will return immediately with `false`.
 * Otherwise, the lock will be acquired and this function will return `true`.
 */
bool flock_spin_try_lock(flock_spin_t* lock);

/**
 * Unlock an ::flock_spin.
 */
void flock_spin_unlock(flock_spin_t* lock);

//
// flock_spin_intsafe_t
//

/*
 * A general-purpose spinlock that can also be locked in an interrupt-safe way.
 */
//typedef <something> flock_spin_intsafe_t;

/*
 * A value that can be used to statically initialize an ::flock_spin_intsafe.
 */
//#define FLOCK_SPIN_INTSAFE_INIT <something>

/**
 * Initializes an ::flock_spin_intsafe at runtime.
 */
void flock_spin_intsafe_init(flock_spin_intsafe_t* lock);

/**
 * Lock an ::flock_spin_intsafe.
 *
 * This function will not return until it is acquired.
 *
 * This function locks the lock in an interrupt-safe way.
 */
void flock_spin_intsafe_lock(flock_spin_intsafe_t* lock);

/**
 * Like flock_spin_intsafe_lock(), but locks the lock in a non-interrupt-safe way.
 */
void flock_spin_intsafe_lock_unsafe(flock_spin_intsafe_t* lock);

/**
 * Try to lock an ::flock_spin_intsafe.
 *
 * If the lock cannot be acquired immediately, this function will return immediately with `false`.
 * Otherwise, the lock will be acquired and this function will return `true`.
 *
 * This function locks the lock in an interrupt-safe way.
 */
bool flock_spin_intsafe_try_lock(flock_spin_intsafe_t* lock);

/**
 * Like flock_spin_intsafe_try_lock(), but locks the lock in a non-interrupt-safe way.
 */
bool flock_spin_intsafe_try_lock_unsafe(flock_spin_intsafe_t* lock);

/**
 * Unlock an ::flock_spin_intsafe.
 *
 * This function unlocks the lock in an interrupt-safe way.
 */
void flock_spin_intsafe_unlock(flock_spin_intsafe_t* lock);

/**
 * Like flock_spin_intsafe_unlock(), but unlocks the lock in a non-interrupt-safe way.
 */
void flock_spin_intsafe_unlock_unsafe(flock_spin_intsafe_t* lock);

/**
 * @}
 */

FERRO_DECLARATIONS_END;

// include the arch-dependent after-header
#if FERRO_ARCH == FERRO_ARCH_x86_64
	#include <ferro/core/x86_64/locks.spin.after.h>
#elif FERRO_ARCH == FERRO_ARCH_aarch64
	#include <ferro/core/aarch64/locks.spin.after.h>
#else
	#error Unrecognized/unsupported CPU architecture! (see <ferro/core/locks.spin.h>)
#endif

#endif // _FERRO_CORE_LOCKS_SPIN_H_
