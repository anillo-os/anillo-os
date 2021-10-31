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
 * AARCH64 implementations of architecture-specific components for interrupts subsystem.
 */

#ifndef _FERRO_CORE_AARCH64_INTERRUPTS_AFTER_H_
#define _FERRO_CORE_AARCH64_INTERRUPTS_AFTER_H_

#include <stdbool.h>

#include <ferro/base.h>
#include <ferro/core/per-cpu.private.h>
#include <ferro/core/interrupts.h>
#include <ferro/core/panic.h>

FERRO_DECLARATIONS_BEGIN;

/**
 * @addtogroup Interrupts
 *
 * @{
 */

#ifndef FARCH_INT_NO_INTERRUPTS_IN_INTERRUPT_CONTEXT
	#define FARCH_INT_NO_INTERRUPTS_IN_INTERRUPT_CONTEXT 1
#endif

FERRO_ALWAYS_INLINE void fint_disable(void) {
	// '\043' == '#'
	__asm__ volatile("msr daifset, \04315" ::: "memory");

	if (__builtin_add_overflow(FARCH_PER_CPU(outstanding_interrupt_disable_count), 1, &FARCH_PER_CPU(outstanding_interrupt_disable_count))) {
		fpanic("Interrupt disable count overflow");
	}
};

FERRO_ALWAYS_INLINE void fint_enable(void) {
	if (__builtin_sub_overflow(FARCH_PER_CPU(outstanding_interrupt_disable_count), 1, &FARCH_PER_CPU(outstanding_interrupt_disable_count))) {
		fpanic("Interrupt disable count underflow");
	}

	if (FARCH_PER_CPU(outstanding_interrupt_disable_count) == 0) {
#if FARCH_INT_NO_INTERRUPTS_IN_INTERRUPT_CONTEXT
		if (fint_is_interrupt_context()) {
			FARCH_PER_CPU(outstanding_interrupt_disable_count) = 1;
			fpanic("Interrupts enabled in interrupt context");
		}
#endif

		// '\043' == '#'
		__asm__ volatile("msr daifclr, \04315" ::: "memory");
	}
};

FERRO_ALWAYS_INLINE fint_state_t fint_save(void) {
	return FARCH_PER_CPU(outstanding_interrupt_disable_count);
};

FERRO_ALWAYS_INLINE void fint_restore(fint_state_t state) {
	// '\043' == '#'
	__asm__ volatile("msr daifset, \04315" ::: "memory");

	FARCH_PER_CPU(outstanding_interrupt_disable_count) = state;

	if (state == 0) {
		__asm__ volatile("msr daifclr, \04315" ::: "memory");
	}
};

typedef void (*farch_int_irq_handler_f)(bool is_fiq, fint_frame_t* frame);

/**
 * Sets the FIQ/IRQ handler for the system.
 */
void farch_int_set_irq_handler(farch_int_irq_handler_f handler);

FERRO_ALWAYS_INLINE bool fint_is_interrupt_context(void) {
	return FARCH_PER_CPU(current_exception_frame) != NULL;
};

FERRO_ALWAYS_INLINE fint_frame_t* fint_current_frame(void) {
	return FARCH_PER_CPU(current_exception_frame);
};

/**
 * @}
 */

FERRO_DECLARATIONS_END;

#endif // _FERRO_CORE_AARCH64_INTERRUPTS_AFTER_H_
