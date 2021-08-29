/*
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

#ifndef _FERRO_CORE_AARCH64_INTERRUPTS_H_
#define _FERRO_CORE_AARCH64_INTERRUPTS_H_

#include <stdbool.h>

#include <ferro/base.h>
#include <ferro/core/aarch64/per-cpu.h>
#include <ferro/core/interrupts.h>

FERRO_DECLARATIONS_BEGIN;

FERRO_PACKED_STRUCT(farch_int_exception_frame) {
	uint64_t x0;
	uint64_t x1;
	uint64_t x2;
	uint64_t x3;
	uint64_t x4;
	uint64_t x5;
	uint64_t x6;
	uint64_t x7;
	uint64_t x8;
	uint64_t x9;
	uint64_t x10;
	uint64_t x11;
	uint64_t x12;
	uint64_t x13;
	uint64_t x14;
	uint64_t x15;
	uint64_t x16;
	uint64_t x17;
	uint64_t x18;
	uint64_t x19;
	uint64_t x20;
	uint64_t x21;
	uint64_t x22;
	uint64_t x23;
	uint64_t x24;
	uint64_t x25;
	uint64_t x26;
	uint64_t x27;
	uint64_t x28;
	uint64_t x29; // fp
	uint64_t x30; // lr
	uint64_t elr;
	uint64_t esr;
	uint64_t far;
	uint64_t sp;

	// actually spsr
	uint64_t pstate;

	uint64_t interrupt_disable;
	uint64_t reserved;
};

// needs to be 16-byte aligned so we can push it onto the stack
FERRO_VERIFY_ALIGNMENT(farch_int_exception_frame_t, 16);

/**
 * The type used to represent the interrupt state returned by `fint_save` and accepted by `fint_restore`.
 */
typedef FARCH_PER_CPU_TYPEOF(outstanding_interrupt_disable_count) fint_state_t;

FERRO_ALWAYS_INLINE void fint_disable(void) {
	if (FARCH_PER_CPU(outstanding_interrupt_disable_count)++ == 0) {
		// '\043' == '#'
		__asm__ volatile("msr daifset, \04315" ::: "memory");
	}
};

FERRO_ALWAYS_INLINE void fint_enable(void) {
	if (--FARCH_PER_CPU(outstanding_interrupt_disable_count) == 0) {
		// '\043' == '#'
		__asm__ volatile("msr daifclr, \04315" ::: "memory");
	}
};

/**
 * Returns the current interrupt state. Useful to save the current state and restore it later.
 */
FERRO_ALWAYS_INLINE fint_state_t fint_save(void) {
	return FARCH_PER_CPU(outstanding_interrupt_disable_count);
};

/**
 * Applies the given interrupt state. Useful to restore a previously saved interrupt state.
 *
 * Note that it is unsafe to use `fint_enable`/`fint_disable` and this function in the same context (as it will lead to the outstanding-interrupt-disable count becoming unbalanced).
 */
FERRO_ALWAYS_INLINE void fint_restore(fint_state_t state) {
	FARCH_PER_CPU(outstanding_interrupt_disable_count) = state;

	// '\043' == '#'
	if (state == 0) {
		__asm__ volatile("msr daifclr, \04315" ::: "memory");
	} else {
		__asm__ volatile("msr daifset, \04315" ::: "memory");
	}
};

typedef void (*farch_int_irq_handler_f)(bool is_fiq, farch_int_exception_frame_t* frame);

/**
 * Sets the FIQ/IRQ handler for the system.
 */
void farch_int_set_irq_handler(farch_int_irq_handler_f handler);

FERRO_ALWAYS_INLINE bool fint_is_interrupt_context(void) {
	return FARCH_PER_CPU(current_exception_frame) != NULL;
};

FERRO_DECLARATIONS_END;

#endif // _FERRO_CORE_AARCH64_INTERRUPTS_H_
