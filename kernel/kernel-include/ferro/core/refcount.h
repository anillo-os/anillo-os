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

#ifndef _FERRO_CORE_REFCOUNT_H_
#define _FERRO_CORE_REFCOUNT_H_

#include <stdint.h>
#include <stdbool.h>

#include <ferro/base.h>
#include <ferro/error.h>

FERRO_DECLARATIONS_BEGIN;

typedef uint64_t frefcount_t;

/**
 * A value that can be used to initialize refcounts statically (i.e. at compile-time).
 */
#define FREFCOUNT_INITIALIZER 1

/**
 * Initializes a refcount.
 *
 * This can be used to initialize refcounts dynamically (i.e. at run-time).
 */
FERRO_ALWAYS_INLINE void frefcount_init(frefcount_t* refcount) {
	*refcount = FREFCOUNT_INITIALIZER;
};

/**
 * Tries to increment the given refcount.
 *
 * @param refcount The refcount to try to increment.
 *
 * Return values:
 * @retval ferr_ok               The refcount was successfully incremented.
 * @retval ferr_permanent_outage The refcount was killed (decremented all the way to zero) while this call occurred. It is no longer valid.
 */
FERRO_ALWAYS_INLINE FERRO_WUR ferr_t frefcount_increment(frefcount_t* refcount) {
	uint64_t old_value = __atomic_load_n(refcount, __ATOMIC_RELAXED);

	do {
		if (old_value == 0) {
			return ferr_permanent_outage;
		}
	} while (!__atomic_compare_exchange_n(refcount, &old_value, old_value + 1, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED));

	return ferr_ok;
};

/**
 * Decrements the given refcount.
 *
 * @param refcount The refcount to decrement.
 *
 * @retval ferr_ok                  The refcount is still alive (greater than zero).
 * @retval ferr_permanent_outage    The refcount is now dead as a result of this call.
 * @retval ferr_already_in_progress The refcount was already dead.
 */
FERRO_ALWAYS_INLINE FERRO_WUR ferr_t frefcount_decrement(frefcount_t* refcount) {
	uint64_t old_value = __atomic_load_n(refcount, __ATOMIC_RELAXED);

	do {
		if (old_value == 0) {
			return ferr_already_in_progress;
		}
	} while (!__atomic_compare_exchange_n(refcount, &old_value, old_value - 1, false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED));

	if (old_value != 1) {
		return ferr_ok;
	}

	return ferr_permanent_outage;
};

FERRO_DECLARATIONS_END;

#endif // _FERRO_CORE_REFCOUNT_H_
