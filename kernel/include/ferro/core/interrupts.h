/**
 * This file is part of Anillo OS
 * Copyright (C) 2020 Anillo OS Developers
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

#ifndef _FERRO_CORE_INTERRUPTS_H_
#define _FERRO_CORE_INTERRUPTS_H_

#include <stdint.h>

#include <ferro/base.h>
#include <ferro/platform.h>

FERRO_DECLARATIONS_BEGIN;

/**
 * Initializes the interrupts subsystem. Called on kernel startup.
 */
void fint_init(void);

// these are arch-dependent functions we expect all architectures to implement

/**
 * Disables all interrupts unconditionally.
 */
FERRO_ALWAYS_INLINE void fint_disable(void);

/**
 * Enables all interrupts unconditionally.
 */
FERRO_ALWAYS_INLINE void fint_enable(void);

/**
 * Returns the current interrupt state. Useful to save the current state and restore it later.
 */
FERRO_ALWAYS_INLINE uint64_t fint_save(void);

/**
 * Applies the given interrupt state. Useful to restore a previously saved interrupt state.
 */
FERRO_ALWAYS_INLINE void fint_restore(uint64_t state);

FERRO_DECLARATIONS_END;

// now include the arch-dependent header
#if FERRO_ARCH == FERRO_ARCH_x86_64
	#include <ferro/core/x86_64/interrupts.h>
#elif FERRO_ARCH == FERRO_ARCH_aarch64
	#include <ferro/core/aarch64/interrupts.h>
#else
	#error Unrecognized/unsupported CPU architecture! (see <ferro/core/interrupts.h>)
#endif

#endif // _FERRO_CORE_INTERRUPTS_H_
