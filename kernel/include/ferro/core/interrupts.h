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
 * Interrupts subsystem.
 */

#ifndef _FERRO_CORE_INTERRUPTS_H_
#define _FERRO_CORE_INTERRUPTS_H_

#include <stdint.h>
#include <stdbool.h>

#include <ferro/base.h>
#include <ferro/platform.h>

// include the arch-dependent before-header
#if FERRO_ARCH == FERRO_ARCH_x86_64
	#include <ferro/core/x86_64/interrupts.before.h>
#elif FERRO_ARCH == FERRO_ARCH_aarch64
	#include <ferro/core/aarch64/interrupts.before.h>
#else
	#error Unrecognized/unsupported CPU architecture! (see <ferro/core/interrupts.h>)
#endif

FERRO_DECLARATIONS_BEGIN;

/**
 * @addtogroup Interrupts
 *
 * The interrupts subsystem.
 *
 * @{
 */

/**
 * Initializes the interrupts subsystem. Called on kernel startup.
 *
 * After this function is called, interrupts are enabled.
 */
void fint_init(void);

// these are arch-dependent functions we expect all architectures to implement

/**
 * Disables all interrupts.
 *
 * This also increments the outstanding-interrupt-disable count.
 * As long as this value is greater than 0, interrupts will not be enabled.
 */
FERRO_ALWAYS_INLINE void fint_disable(void);

/**
 * Enables all interrupts.
 *
 * This first decrements the outstanding-interrupt-disable count. If this value is now 0, interrupts are enabled.
 * Otherwise, interrupts remain enabled.
 */
FERRO_ALWAYS_INLINE void fint_enable(void);

/**
 * Returns the current interrupt state. Useful to save the current state and restore it later.
 */
FERRO_ALWAYS_INLINE fint_state_t fint_save(void);

/**
 * Applies the given interrupt state. Useful to restore a previously saved interrupt state.
 *
 * Note that it is unsafe to use fint_enable()/fint_disable() and this function in the same context (as it will lead to the outstanding-interrupt-disable count becoming unbalanced).
 */
FERRO_ALWAYS_INLINE void fint_restore(fint_state_t state);

/**
 * Checks whether we're currently running in an interrupt context.
 */
FERRO_ALWAYS_INLINE bool fint_is_interrupt_context(void);

/**
 * @}sw
 */

FERRO_DECLARATIONS_END;

// now include the arch-dependent after-header
#if FERRO_ARCH == FERRO_ARCH_x86_64
	#include <ferro/core/x86_64/interrupts.after.h>
#elif FERRO_ARCH == FERRO_ARCH_aarch64
	#include <ferro/core/aarch64/interrupts.after.h>
#else
	#error Unrecognized/unsupported CPU architecture! (see <ferro/core/interrupts.h>)
#endif

#endif // _FERRO_CORE_INTERRUPTS_H_
