/**
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
//
// src/core/aarch64/interrupts.c
//
// AARCH64 interrupt handling
//

#include <ferro/core/interrupts.h>
#include <ferro/core/panic.h>
#include <ferro/core/console.h>
#include <ferro/core/locks.h>

FERRO_PACKED_STRUCT(fint_vector_table_block) {
	uint8_t synchronous[0x80];
	uint8_t irq[0x80];
	uint8_t fiq[0x80];
	uint8_t serror[0x80];
};

FERRO_PACKED_STRUCT(fint_vector_table) {
	fint_vector_table_block_t current_with_sp0;
	fint_vector_table_block_t current_with_spx;
	fint_vector_table_block_t lower_with_aarch64;
	fint_vector_table_block_t lower_with_aarch32;
};

FERRO_ENUM(uint8_t, fint_esr_code) {
	fint_esr_code_svc64                      = 0x15,
	fint_esr_code_instruction_abort_lower_el = 0x20,
	fint_esr_code_instruction_abort_same_el  = 0x21,
	fint_esr_code_pc_alignment_fault         = 0x22,
	fint_esr_code_data_abort_lower_el        = 0x24,
	fint_esr_code_data_abort_same_el         = 0x25,
	fint_esr_code_sp_alignment_fault         = 0x26,
	fint_esr_code_serror                     = 0x2f,
	fint_esr_code_breakpoint_lower_el        = 0x40,
	fint_esr_code_breakpoint_same_el         = 0x41,
	fint_esr_code_software_step_lower_el     = 0x32,
	fint_esr_code_software_step_same_el      = 0x33,
	fint_esr_code_watchpoint_lower_el        = 0x34,
	fint_esr_code_watchpoint_same_el         = 0x35,
	fint_esr_code_brk                        = 0x3c,
};

FERRO_STRUCT(fint_handler_common_data) {
	fint_state_t interrupt_state;
};

extern fint_vector_table_t fint_ivt;

static farch_int_irq_handler_f irq_handler = NULL;
static flock_spin_intsafe_t irq_handler_lock = FLOCK_SPIN_INTSAFE_INIT;

FERRO_ALWAYS_INLINE fint_esr_code_t code_from_esr(uint64_t esr) {
	return (esr & (0x3fULL << 26)) >> 26;
};

static void fint_handler_common_begin(fint_handler_common_data_t* data) {
	// ARM automatically disables interrupts when handling an interrupt
	// so we need to let our interrupt management code know this
	data->interrupt_state = FARCH_PER_CPU(outstanding_interrupt_disable_count);
	FARCH_PER_CPU(outstanding_interrupt_disable_count) = 1;
};

static void fint_handler_common_end(fint_handler_common_data_t* data) {
	FARCH_PER_CPU(outstanding_interrupt_disable_count) = data->interrupt_state;
};

void fint_handler_current_with_spx_sync(fint_exception_frame_t* frame) {
	fint_handler_common_data_t data;
	fint_esr_code_t code;

	fint_handler_common_begin(&data);

	code = code_from_esr(frame->esr);

	switch (code) {
		case fint_esr_code_svc64: {
			fconsole_logf("received an SVC from the kernel at %p\n", (void*)(frame->elr - 4));
		} break;

		case fint_esr_code_instruction_abort_same_el: {
			fconsole_logf("instruction abort at %p on address %p\n", (void*)frame->elr, (void*)frame->far);
			fpanic("instruction abort in kernel");
		} break;

		case fint_esr_code_data_abort_same_el: {
			fconsole_logf("instruction abort at %p on address %p\n", (void*)frame->elr, (void*)frame->far);
			fpanic("data abort in kernel");
		} break;

		case fint_esr_code_brk:
		case fint_esr_code_breakpoint_same_el: {
			fconsole_logf("breakpoint at %p\n", (void*)frame->elr);
			frame->elr += 4;
		} break;

		case fint_esr_code_software_step_same_el: {
			fconsole_logf("software step at %p\n", (void*)frame->elr);
			frame->elr += 4;
		} break;

		case fint_esr_code_watchpoint_same_el: {
			fconsole_logf("watchpoint hit at %p on address %p\n", (void*)frame->elr, (void*)frame->far);
			frame->elr += 4;
		} break;

		// since we don't have userspace implemented yet, these should never occur
		case fint_esr_code_instruction_abort_lower_el:
		case fint_esr_code_data_abort_lower_el:
		case fint_esr_code_breakpoint_lower_el:
		case fint_esr_code_software_step_lower_el:
		case fint_esr_code_watchpoint_lower_el:

		// well, crap, we don't know what this is about! just die.
		default: {
			fpanic("invalid synchronous exception: %u; generated at %p", code, (void*)frame->elr);
		} break;
	}

	fint_handler_common_end(&data);
};

void fint_handler_current_with_spx_irq(fint_exception_frame_t* frame) {
	fint_handler_common_data_t data;
	farch_int_irq_handler_f handler = NULL;

	fint_handler_common_begin(&data);

	flock_spin_intsafe_lock(&irq_handler_lock);
	handler = irq_handler;
	flock_spin_intsafe_unlock(&irq_handler_lock);

	if (handler) {
		handler(false, frame);
	} else {
		fpanic("No FIQ/IRQ handler set");
	}

	fint_handler_common_end(&data);
};

void fint_handler_current_with_spx_fiq(fint_exception_frame_t* frame) {
	fint_handler_common_data_t data;
	farch_int_irq_handler_f handler = NULL;

	fint_handler_common_begin(&data);

	flock_spin_intsafe_lock(&irq_handler_lock);
	handler = irq_handler;
	flock_spin_intsafe_unlock(&irq_handler_lock);

	if (handler) {
		handler(true, frame);
	} else {
		fpanic("No FIQ/IRQ handler set");
	}

	fint_handler_common_end(&data);
};

void fint_handler_current_with_spx_serror(fint_exception_frame_t* frame) {
	fint_handler_common_data_t data;
	fint_handler_common_begin(&data);

	// SErrors are generally unrecoverable, so just die
	fpanic("serror");

	// unnecessary, but just for consistency
	fint_handler_common_end(&data);
};

void fint_handler_current_with_sp0_sync(fint_exception_frame_t* frame) {
	return fint_handler_current_with_spx_sync(frame);
};

void fint_handler_current_with_sp0_irq(fint_exception_frame_t* frame) {
	return fint_handler_current_with_spx_irq(frame);
};

void fint_handler_current_with_sp0_fiq(fint_exception_frame_t* frame) {
	return fint_handler_current_with_spx_fiq(frame);
};

void fint_handler_current_with_sp0_serror(fint_exception_frame_t* frame) {
	return fint_handler_current_with_spx_serror(frame);
};

void fint_init(void) {
	__asm__ volatile("msr VBAR_EL1, %0" :: "r" (&fint_ivt) : "memory");

	fint_enable();

	// test: hit a breakpoint
	//__builtin_debugtrap();
};

void farch_int_set_irq_handler(farch_int_irq_handler_f handler) {
	flock_spin_intsafe_lock(&irq_handler_lock);
	irq_handler = handler;
	flock_spin_intsafe_unlock(&irq_handler_lock);
};
