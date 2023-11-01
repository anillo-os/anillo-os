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
#include <stddef.h>

#include <ferro/base.h>
#include <ferro/platform.h>
#include <ferro/error.h>

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
 * These are special interrupts that are present on all architectures.
 */
FERRO_ENUM(uint8_t, fint_special_interrupt_common) {
	/**
	 * Triggered when a breakpoint is hit.
	 *
	 * On all architectures, the frame for this interrupt will have the instruction pointer set to the address of the instruction that caused the breakpoint
	 * (e.g. the `int3` instruction on x86).
	 */
	fint_special_interrupt_common_breakpoint,

	/**
	 * Triggered when the single-step flag is set in the processor and a single instruction has been executed.
	 *
	 * On all architectures, the single-step flag will be cleared in the flags saved into the interrupt frame generated by this interrupt.
	 * Unlike the breakpoint interrupt, the instruction pointer will be set to the instruction following the one that was executed.
	 */
	fint_special_interrupt_common_single_step,

	/**
	 * Triggered when a watchpoint is hit.
	 */
	fint_special_interrupt_common_watchpoint,

	/**
	 * Triggered when an invalid page is accessed.
	 */
	fint_special_interrupt_page_fault,

	/**
	 * Triggered when an attempt is made to execute an invalid instruction.
	 */
	fint_special_interrupt_invalid_instruction,

	/**
	 * Not a special interrupt number; only used as the last member of the enum.
	 */
	fint_special_interrupt_common_LAST,
};

/**
 * Called when a special interrupt (i.e. one of the ones listed in ::fint_special_interrupt_common or ::fint_special_interrupt_arch) is triggered.
 *
 * @param data User-defined data provided to fint_register_special_handler() during registration.
 */
typedef void (*fint_special_handler_f)(void* data);

/**
 * Initializes the interrupts subsystem. Called on kernel startup.
 *
 * After this function is called, interrupts are enabled.
 */
void fint_init(void);

/**
 * Initializes the interrupts subsystem for a secondary CPU.
 *
 * After this function is called, interrupts are enabled.
 */
void fint_init_secondary_cpu(void);

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
 * Returns the interrupt frame for the current interrupt, if any.
 *
 * @returns The interrupt frame for the current interrupt, if currently in an interrupt, or `NULL` otherwise.
 */
FERRO_ALWAYS_INLINE fint_frame_t* fint_current_frame(void);

/**
 * Registers the given handler to be called when the given special interrupt is triggered.
 *
 * @param number  The number of the special interrupt to register for.
 * @param handler The handler to invoke.
 * @param data    User-defined data to pass to the handler.
 *
 * @retval ferr_ok               The handler was successfully registered.
 * @retval ferr_temporary_outage There was already a handler registered for that special interrupt.
 * @retval ferr_invalid_argument One or more of: 1) @p number was outside the range supported by the system, or 2) @p handler was `NULL`.
 */
ferr_t fint_register_special_handler(uint8_t number, fint_special_handler_f handler, void* data);

void fint_log_frame(const fint_frame_t* frame);

void fint_trace_interrupted_stack(const fint_frame_t* frame);

void fint_trace_current_stack(void);

FERRO_ALWAYS_INLINE fint_frame_t* fint_root_frame(fint_frame_t* frame) {
	while (frame && frame->previous_frame != NULL) {
		frame = frame->previous_frame;
	}
	return frame;
};

/**
 * @}
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