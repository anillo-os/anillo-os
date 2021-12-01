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
 * x86_64 implementations of architecture-specific components for interrupts subsystem.
 */

#ifndef _FERRO_CORE_X86_64_INTERRUPTS_AFTER_H_
#define _FERRO_CORE_X86_64_INTERRUPTS_AFTER_H_

#include <stdbool.h>

#include <ferro/base.h>
#include <ferro/core/per-cpu.private.h>
#include <ferro/core/interrupts.h>
#include <ferro/error.h>

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
	__asm__ volatile("cli" ::: "memory");
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
		__asm__ volatile("sti" ::: "memory");
	}
};

FERRO_ALWAYS_INLINE uint64_t farch_int_save_flags(void) {
	uint64_t flags = 0;

	__asm__ volatile(
		"pushfq\n"
		"pop %0\n"
		:
		"=rm" (flags)
		::
		"memory"
	);

	return flags;
};

FERRO_ALWAYS_INLINE fint_state_t fint_save(void) {
	return FARCH_PER_CPU(outstanding_interrupt_disable_count);
};

FERRO_ALWAYS_INLINE void fint_restore(fint_state_t state) {
	__asm__ volatile("cli" ::: "memory");

	FARCH_PER_CPU(outstanding_interrupt_disable_count) = state;

	if (state == 0) {
		__asm__ volatile("sti" ::: "memory");
	}
};

FERRO_ENUM(uint8_t, farch_int_gdt_index) {
	farch_int_gdt_index_null,
	farch_int_gdt_index_code,
	farch_int_gdt_index_data,
	farch_int_gdt_index_tss,
	farch_int_gdt_index_tss_other,
	farch_int_gdt_index_data_user,
	farch_int_gdt_index_code_user,
};

/**
 * A handler that is to be called when an interrupt is received.
 *
 * The handler ***is*** allowed to modify the given frame, which may alter the state of the processor upon return.
 *
 * The handler is called with interrupts disabled.
 */
typedef void (*farch_int_handler_f)(fint_frame_t* frame);

/**
 * Registers the given handler for the given interrupt number.
 *
 * @note This function CANNOT be used to register handlers for the first 32 processor-reserved interrupts.
 *
 * @param interrupt The interrupt number to register the handler for.
 * @param handler   The handler to call when the interrupt is received. See ::farch_int_handler_f for more details.
 *
 * Return values:
 * @retval ferr_ok               The handler was registered successfully.
 * @retval ferr_invalid_argument One or more of: 1) the given interrupt number is outside the permitted range (32-255, inclusive), 2) the handler is `NULL`.
 * @retval ferr_temporary_outage A handler for the given interrupt is already registered and must be explicitly unregistered with farch_int_unregister_handler().
 */
FERRO_WUR ferr_t farch_int_register_handler(uint8_t interrupt, farch_int_handler_f handler);

/**
 * Unregisters the handler for the given interrupt number.
 *
 * @param interrupt The interrupt number for which to unregister the handler.
 *
 * Returns values:
 * @retval ferr_ok               The handler for the interrupt was successfully unregistered.
 * @retval ferr_invalid_argument The given interrupt number is outside the permitted range (32-255, inclusive).
 * @retval ferr_no_such_resource There is no handler registered for the given interrupt number.
 */
FERRO_WUR ferr_t farch_int_unregister_handler(uint8_t interrupt);

/**
 * Returns the number of the next unused/unregistered interrupt, or `0` if all interrupts are in-use/registered.
 *
 * @note This is a costly operation.
 *
 * @note By the time the function returns, the number returned may have already been registered. Thus, if this is used to determine
 *       an interrupt number to register, you MUST check the return code of farch_int_register_handler().
 */
uint8_t farch_int_next_available(void);

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

#endif // _FERRO_CORE_X86_64_INTERRUPTS_AFTER_H_