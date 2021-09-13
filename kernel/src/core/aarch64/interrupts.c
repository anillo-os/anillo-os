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
 * AARCH64 interrupt handling.
 */

#include <ferro/core/interrupts.h>
#include <ferro/core/panic.h>
#include <ferro/core/console.h>
#include <ferro/core/locks.h>
#include <ferro/core/paging.h>
#include <ferro/core/threads.private.h>

#define EXCEPTION_STACK_SIZE (2ULL * 1024 * 1024)

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
	fint_frame_t* previous_exception_frame;
};

extern fint_vector_table_t fint_ivt;

static farch_int_irq_handler_f irq_handler = NULL;
static flock_spin_intsafe_t irq_handler_lock = FLOCK_SPIN_INTSAFE_INIT;

FERRO_ALWAYS_INLINE fint_esr_code_t code_from_esr(uint64_t esr) {
	return (esr >> 26) & 0x3fULL;
};

FERRO_ALWAYS_INLINE uint32_t iss_from_esr(uint64_t esr) {
	return esr & 0x1ffffffULL;
};

static void fint_handler_common_begin(fint_handler_common_data_t* data, fint_frame_t* frame) {
	data->previous_exception_frame = FARCH_PER_CPU(current_exception_frame);
	FARCH_PER_CPU(current_exception_frame) = frame;

	// ARM automatically disables interrupts when handling an interrupt
	// so we need to let our interrupt management code know this
	frame->interrupt_disable = FARCH_PER_CPU(outstanding_interrupt_disable_count);
	FARCH_PER_CPU(outstanding_interrupt_disable_count) = 1;

	if (FARCH_PER_CPU(current_thread)) {
		fthread_interrupt_start(FARCH_PER_CPU(current_thread));
	}
};

static void fint_handler_common_end(fint_handler_common_data_t* data, fint_frame_t* frame) {
	if (FARCH_PER_CPU(current_thread)) {
		fthread_interrupt_end(FARCH_PER_CPU(current_thread));
	}

	FARCH_PER_CPU(outstanding_interrupt_disable_count) = frame->interrupt_disable;

	FARCH_PER_CPU(current_exception_frame) = data->previous_exception_frame;
};

void fint_handler_current_with_spx_sync(fint_frame_t* frame) {
	fint_handler_common_data_t data;
	fint_esr_code_t code;
	uint32_t iss;

	fint_handler_common_begin(&data, frame);

	code = code_from_esr(frame->esr);
	iss = iss_from_esr(frame->esr);

	switch (code) {
		case fint_esr_code_svc64: {
			if (iss == 0xfffe) {
				// this is the interrupt used for a thread to preempt itself immediately.
				// we can just do nothing here; the threading subsystem's interrupt hooks will take care of switching threads around.

				//fconsole_log("received SVC for thread preemption\n");
			} else {
				fconsole_logf("received an SVC from the kernel at %p\n", (void*)(frame->elr - 4));
			}
		} break;

		case fint_esr_code_instruction_abort_same_el: {
			fconsole_logf("instruction abort at %p on address %p\n", (void*)frame->elr, (void*)frame->far);
			fpanic("instruction abort in kernel");
		} break;

		case fint_esr_code_data_abort_same_el: {
			fconsole_logf("data abort at %p on address %p\n", (void*)frame->elr, (void*)frame->far);
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

	fint_handler_common_end(&data, frame);
};

void fint_handler_current_with_spx_irq(fint_frame_t* frame) {
	fint_handler_common_data_t data;
	farch_int_irq_handler_f handler = NULL;

	fint_handler_common_begin(&data, frame);

	flock_spin_intsafe_lock(&irq_handler_lock);
	handler = irq_handler;
	flock_spin_intsafe_unlock(&irq_handler_lock);

	if (handler) {
		handler(false, frame);
	} else {
		fpanic("No FIQ/IRQ handler set");
	}

	fint_handler_common_end(&data, frame);
};

void fint_handler_current_with_spx_fiq(fint_frame_t* frame) {
	fint_handler_common_data_t data;
	farch_int_irq_handler_f handler = NULL;

	fint_handler_common_begin(&data, frame);

	flock_spin_intsafe_lock(&irq_handler_lock);
	handler = irq_handler;
	flock_spin_intsafe_unlock(&irq_handler_lock);

	if (handler) {
		handler(true, frame);
	} else {
		fpanic("No FIQ/IRQ handler set");
	}

	fint_handler_common_end(&data, frame);
};

void fint_handler_current_with_spx_serror(fint_frame_t* frame) {
	fint_handler_common_data_t data;
	fint_handler_common_begin(&data, frame);

	// SErrors are generally unrecoverable, so just die
	fpanic("serror");

	// unnecessary, but just for consistency
	fint_handler_common_end(&data, frame);
};

void fint_handler_current_with_sp0_sync(fint_frame_t* frame) {
	return fint_handler_current_with_spx_sync(frame);
};

void fint_handler_current_with_sp0_irq(fint_frame_t* frame) {
	return fint_handler_current_with_spx_irq(frame);
};

void fint_handler_current_with_sp0_fiq(fint_frame_t* frame) {
	return fint_handler_current_with_spx_fiq(frame);
};

void fint_handler_current_with_sp0_serror(fint_frame_t* frame) {
	return fint_handler_current_with_spx_serror(frame);
};

void fint_init(void) {
	void* exception_stack = NULL;

	__asm__ volatile("msr vbar_el1, %0" :: "r" (&fint_ivt));

	// allocate a stack for exceptions
	if (fpage_allocate_kernel(fpage_round_up_to_page_count(EXCEPTION_STACK_SIZE), &exception_stack) != ferr_ok) {
		fpanic("Failed to allocate exception stack");
	}

	exception_stack = (void*)((uintptr_t)exception_stack + EXCEPTION_STACK_SIZE);

	// why make this unnecessarily complicated, ARM?
	// we have to first temporarily switch to the spx stack, set the new value using sp, and then switch back.
	// because for reason, we shouldn't be allowed to write to OUR OWN EL STACK!!!
	__asm__ volatile(
		// '\043' == '#'
		"msr spsel, \0431\n"
		"mov sp, %0\n"
		"msr spsel, \0430\n"
		::
		"r" (exception_stack)
	);

	fint_enable();

	// test: hit a breakpoint
	//__builtin_debugtrap();
};

void farch_int_set_irq_handler(farch_int_irq_handler_f handler) {
	flock_spin_intsafe_lock(&irq_handler_lock);
	irq_handler = handler;
	flock_spin_intsafe_unlock(&irq_handler_lock);
};

ferr_t fint_register_special_handler(uint8_t number, fint_special_handler_f handler, void* data) {
	return ferr_invalid_argument;
};
