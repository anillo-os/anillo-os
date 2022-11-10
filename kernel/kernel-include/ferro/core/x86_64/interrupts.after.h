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

#include <ferro/core/x86_64/interrupts.defs.h>

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

/**
 * A handler that is to be called when an interrupt is received.
 *
 * The handler ***is*** allowed to modify the given frame, which may alter the state of the processor upon return.
 *
 * The handler is called with interrupts disabled.
 */
typedef void (*farch_int_handler_f)(void* data, fint_frame_t* frame);

FERRO_OPTIONS(uint64_t, farch_int_handler_flags) {
	farch_int_handler_flag_safe_mode = 1 << 0,
};

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
FERRO_WUR ferr_t farch_int_register_handler(uint8_t interrupt, farch_int_handler_f handler, void* data, farch_int_handler_flags_t flags);

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
 * Registers the given handler for the next available interrupt number and returns the number it was registered on.
 *
 * @param handler       The handler to call when the interrupt is received. See ::farch_int_handler_f for more details.
 * @param out_interrupt A pointer in which the registered interrupt number will be written.
 *
 * Return values:
 * @retval ferr_ok               The handler was registered successfully and the registered interrupt number has been written to @p out_interrupt.
 * @retval ferr_invalid_argument One or more of: 1) @p handler was `NULL`, 2) @p out_interrupt was `NULL`.
 * @retval ferr_temporary_outage There were no available interrupts.
 */
FERRO_WUR ferr_t farch_int_register_next_available(farch_int_handler_f handler, void* data, uint8_t* out_interrupt, farch_int_handler_flags_t flags);

FERRO_ALWAYS_INLINE bool fint_is_interrupt_context(void) {
	return FARCH_PER_CPU(current_exception_frame) != NULL;
};

FERRO_ALWAYS_INLINE fint_frame_t* fint_current_frame(void) {
	return FARCH_PER_CPU(current_exception_frame);
};

FERRO_ALWAYS_INLINE void fint_make_idt_entry(farch_int_idt_entry_t* out_entry, void* isr, uint8_t code_segment_index, uint8_t ist_index, bool enable_interrupts, uint8_t privilege_level) {
	uintptr_t isr_addr = (uintptr_t)isr;

	out_entry->pointer_low_16 = isr_addr & 0xffffULL;
	out_entry->pointer_mid_16 = (isr_addr & (0xffffULL << 16)) >> 16;
	out_entry->pointer_high_32 = (isr_addr & (0xffffffffULL << 32)) >> 32;

	out_entry->code_segment_index = code_segment_index * 8;

	out_entry->options = 0xe00 | (enable_interrupts ? farch_int_idt_entry_option_enable_interrupts : 0) | farch_int_idt_entry_option_present | ((privilege_level & 3) << 13) | (ist_index & 7);

	out_entry->reserved = 0;
};

/**
 * @}
 */

FERRO_DECLARATIONS_END;

#endif // _FERRO_CORE_X86_64_INTERRUPTS_AFTER_H_
