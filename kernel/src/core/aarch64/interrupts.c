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

FERRO_STRUCT(fint_handler_common_data) {
	fint_frame_t* previous_exception_frame;
};

FERRO_STRUCT(farch_int_special_handler_entry) {
	fint_special_handler_f handler;
	void* data;
	flock_spin_intsafe_t lock;
};

extern fint_vector_table_t fint_ivt;

static farch_int_irq_handler_f irq_handler = NULL;
static flock_spin_intsafe_t irq_handler_lock = FLOCK_SPIN_INTSAFE_INIT;
static farch_int_lower_el_handler_f lower_el_handler = NULL;
static flock_spin_intsafe_t lower_el_handler_lock = FLOCK_SPIN_INTSAFE_INIT;
#define SPECIAL_HANDLERS_MAX fint_special_interrupt_common_LAST
static farch_int_special_handler_entry_t special_handlers[SPECIAL_HANDLERS_MAX] = {
	[0 ... (SPECIAL_HANDLERS_MAX - 1)] = {
		.data = NULL,
		.handler = NULL,
		.lock = FLOCK_SPIN_INTSAFE_INIT,
	}
};

FERRO_ALWAYS_INLINE farch_int_esr_code_t code_from_esr(uint64_t esr) {
	return (esr >> 26) & 0x3fULL;
};

FERRO_ALWAYS_INLINE uint32_t iss_from_esr(uint64_t esr) {
	return esr & 0x1ffffffULL;
};

static void fint_handler_common_begin(fint_handler_common_data_t* data, fint_frame_t* frame, bool safe_mode) {
	frame->previous_frame = FARCH_PER_CPU(current_exception_frame);
	FARCH_PER_CPU(current_exception_frame) = frame;

	// ARM automatically disables interrupts when handling an interrupt
	// so we need to let our interrupt management code know this
	frame->interrupt_disable = FARCH_PER_CPU(outstanding_interrupt_disable_count);
	FARCH_PER_CPU(outstanding_interrupt_disable_count) = 1;

	// we also need to save the current address space
	frame->address_space = (uintptr_t)FARCH_PER_CPU(address_space);

	if (!safe_mode && FARCH_PER_CPU(current_thread)) {
		fthread_interrupt_start(FARCH_PER_CPU(current_thread));
	}
};

static void fint_handler_common_end(fint_handler_common_data_t* data, fint_frame_t* frame, bool safe_mode) {
	if (FARCH_PER_CPU(current_thread)) {
		// HACK: see x86_64/interrupts.c
		fthread_interrupt_end(FARCH_PER_CPU(current_thread));
	}

	fpanic_status(fpage_space_swap((void*)frame->address_space));

	FARCH_PER_CPU(outstanding_interrupt_disable_count) = frame->interrupt_disable;

	FARCH_PER_CPU(current_exception_frame) = frame->previous_frame;
};

void farch_int_print_frame(const fint_frame_t* frame) {
	fconsole_logf(
		"x0=%llu, x1=%llu\n"
		"x2=%llu, x3=%llu\n"
		"x4=%llu, x5=%llu\n"
		"x6=%llu, x7=%llu\n"
		"x8=%llu, x9=%llu\n"
		"x10=%llu, x11=%llu\n"
		"x12=%llu, x13=%llu\n"
		"x14=%llu, x15=%llu\n"
		"x16=%llu, x17=%llu\n"
		"x18=%llu, x19=%llu\n"
		"x20=%llu, x21=%llu\n"
		"x22=%llu, x23=%llu\n"
		"x24=%llu, x25=%llu\n"
		"x26=%llu, x27=%llu\n"
		"x28=%llu, x29=%llu\n"
		"x30=%llu, elr=%llu\n"
		"esr=%llu, far=%llu\n"
		"sp=%llu, pstate=%llu\n"
		"interrupt_disable=%llu\n"
		,
		 frame->x0, frame->x1,
		 frame->x2, frame->x3,
		 frame->x4, frame->x5,
		 frame->x6, frame->x7,
		 frame->x8, frame->x9,
		frame->x10, frame->x11,
		frame->x12, frame->x13,
		frame->x14, frame->x15,
		frame->x16, frame->x17,
		frame->x18, frame->x19,
		frame->x20, frame->x21,
		frame->x22, frame->x23,
		frame->x24, frame->x25,
		frame->x26, frame->x27,
		frame->x28, frame->x29,
		frame->x30, frame->elr,
		frame->esr, frame->far,
		frame->sp,  frame->pstate,
		frame->interrupt_disable
	);
};

void fint_log_frame(const fint_frame_t* frame) {
	farch_int_print_frame(frame);
};

FERRO_PACKED_STRUCT(fint_stack_frame) {
	fint_stack_frame_t* previous_frame;
	void* return_address;
};

// exactly the same as x86_64, actually.
static void trace_stack(const fint_stack_frame_t* frame) {
	fconsole_log("stack trace:\n");
	for (size_t i = 0; i < 20; ++i) {
		if (
			frame == 0 || (
				// if we can't find it in the kernel address space AND
				fpage_virtual_to_physical((uintptr_t)frame) == UINTPTR_MAX &&
				// we can't find it in the active address space (if we have one at all)
				fpage_space_virtual_to_physical(fpage_space_current(), (uintptr_t)frame) == UINTPTR_MAX
			)
		) {
			// then this is an invalid address. stop the stack trace here.
			break;
		}

		fconsole_logf("%p\n", frame->return_address);

		frame = frame->previous_frame;
	}
};

void fint_trace_current_stack(void) {
	uint64_t fp;
	__asm__ volatile("mov %0, fp" : "=r" (fp));
	trace_stack((void*)fp);
};

void fint_trace_interrupted_stack(const fint_frame_t* frame) {
	trace_stack((void*)frame->x29);
};

bool farch_int_invoke_special_handler(fint_special_interrupt_common_t id) {
	farch_int_special_handler_entry_t* entry = &special_handlers[id];
	fint_special_handler_f handler = NULL;
	void* handler_data = NULL;

	if (entry) {
		flock_spin_intsafe_lock(&entry->lock);
		handler = entry->handler;
		handler_data = entry->data;
		flock_spin_intsafe_unlock(&entry->lock);
	}

	if (handler) {
		handler(handler_data);
		return true;
	}

	return false;
};

static void fint_handler_synchronous(fint_frame_t* frame, uint8_t exception_level) {
	fint_handler_common_data_t data;
	farch_int_esr_code_t code;
	uint32_t iss;
	bool safe_mode = true;

	code = code_from_esr(frame->esr);
	iss = iss_from_esr(frame->esr);

	// this is the interrupt used for a thread to preempt itself immediately;
	// we specifically DON'T want to use safe mode in this case;
	// we definitely want the scheduler to do its processing.
	if (exception_level == 1 && code == farch_int_esr_code_svc64 && iss == 0xfffe) {
		safe_mode = false;
	}

	fint_handler_common_begin(&data, frame, safe_mode);

	#define CHECK_EL(expected) \
		if (exception_level != expected) { \
			fpanic("invalid exception level %u for interrupt", exception_level); \
		}

	switch (code) {
		case farch_int_esr_code_svc64: {
			if (exception_level == 1) {
				if (iss == 0xfffe) {
					// this is the interrupt used for a thread to preempt itself immediately.
					// we can just do nothing here; the threading subsystem's interrupt hooks will take care of switching threads around.

					//fconsole_log("received SVC for thread preemption\n");
				} else {
					fconsole_logf("received an SVC from the kernel at %p\n", (void*)(frame->elr - 4));
				}
			} else {
				goto handle_lower_el;
			}
		} break;

		case farch_int_esr_code_instruction_abort_same_el: {
			CHECK_EL(1);

			if (!farch_int_invoke_special_handler(fint_special_interrupt_page_fault)) {
				fconsole_logf("instruction abort at %p on address %p\n", (void*)frame->elr, (void*)frame->far);
				farch_int_print_frame(frame);
				fpanic("instruction abort in kernel");
			}
		} break;

		case farch_int_esr_code_data_abort_same_el: {
			CHECK_EL(1);

			if (!farch_int_invoke_special_handler(fint_special_interrupt_page_fault)) {
				fconsole_logf("data abort at %p on address %p\n", (void*)frame->elr, (void*)frame->far);
				farch_int_print_frame(frame);
				fpanic("data abort in kernel");
			}
		} break;

		case farch_int_esr_code_brk:
		case farch_int_esr_code_breakpoint_same_el: {
			if (code == farch_int_esr_code_breakpoint_same_el) {
				CHECK_EL(1);
			}
			fconsole_logf("breakpoint at %p\n", (void*)frame->elr);
			frame->elr += 4;
		} break;

		case farch_int_esr_code_software_step_same_el: {
			CHECK_EL(1);
			fconsole_logf("software step at %p\n", (void*)frame->elr);
			frame->elr += 4;
		} break;

		case farch_int_esr_code_watchpoint_same_el: {
			CHECK_EL(1);
			fconsole_logf("watchpoint hit at %p on address %p\n", (void*)frame->elr, (void*)frame->far);
			frame->elr += 4;
		} break;

		case farch_int_esr_code_instruction_abort_lower_el:
		case farch_int_esr_code_data_abort_lower_el:
		case farch_int_esr_code_breakpoint_lower_el:
		case farch_int_esr_code_software_step_lower_el:
		case farch_int_esr_code_watchpoint_lower_el: {
handle_lower_el:
			CHECK_EL(0);

			farch_int_lower_el_handler_f handler = NULL;

			flock_spin_intsafe_lock(&lower_el_handler_lock);
			handler = lower_el_handler;
			flock_spin_intsafe_unlock(&lower_el_handler_lock);

			if (handler) {
				handler(frame, code, iss);
			} else {
				fpanic("No handler set for synchronous exceptions from lower ELs");
			}
		} break;

		// well, crap, we don't know what this is about! just die.
		default: {
			fint_log_frame(fint_current_frame());
			fint_trace_interrupted_stack(fint_current_frame());
			fpanic("invalid synchronous exception: %u; generated at %p", code, (void*)frame->elr);
		} break;
	}

	fint_handler_common_end(&data, frame, safe_mode);
};

void fint_handler_current_with_spx_sync(fint_frame_t* frame) {
	// we assume EL1 here
	fint_handler_synchronous(frame, 1);
};

void fint_handler_current_with_spx_irq(fint_frame_t* frame) {
	fint_handler_common_data_t data;
	farch_int_irq_handler_f handler = NULL;

	fint_handler_common_begin(&data, frame, false);

	flock_spin_intsafe_lock(&irq_handler_lock);
	handler = irq_handler;
	flock_spin_intsafe_unlock(&irq_handler_lock);

	if (handler) {
		handler(false, frame);
	} else {
		fpanic("No FIQ/IRQ handler set");
	}

	fint_handler_common_end(&data, frame, false);
};

void fint_handler_current_with_spx_fiq(fint_frame_t* frame) {
	fint_handler_common_data_t data;
	farch_int_irq_handler_f handler = NULL;

	fint_handler_common_begin(&data, frame, false);

	flock_spin_intsafe_lock(&irq_handler_lock);
	handler = irq_handler;
	flock_spin_intsafe_unlock(&irq_handler_lock);

	if (handler) {
		handler(true, frame);
	} else {
		fpanic("No FIQ/IRQ handler set");
	}

	fint_handler_common_end(&data, frame, false);
};

void fint_handler_current_with_spx_serror(fint_frame_t* frame) {
	fint_handler_common_data_t data;
	fint_handler_common_begin(&data, frame, true);

	// SErrors are generally unrecoverable, so just die
	fpanic("serror");

	// unnecessary, but just for consistency
	fint_handler_common_end(&data, frame, true);
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

void fint_handler_lower_with_aarch64_sync(fint_frame_t* frame) {
	// we assume EL0 here
	return fint_handler_synchronous(frame, 0);
};

void fint_handler_lower_with_aarch64_irq(fint_frame_t* frame) {
	return fint_handler_current_with_spx_irq(frame);
};

void fint_handler_lower_with_aarch64_fiq(fint_frame_t* frame) {
	return fint_handler_current_with_spx_fiq(frame);
};

void fint_handler_lower_with_aarch64_serror(fint_frame_t* frame) {
	return fint_handler_current_with_spx_serror(frame);
};

void fint_init(void) {
	void* exception_stack = NULL;

	__asm__ volatile("msr vbar_el1, %0" :: "r" (&fint_ivt));

	// allocate a stack for exceptions
	if (fpage_allocate_kernel(fpage_round_up_to_page_count(EXCEPTION_STACK_SIZE), &exception_stack, fpage_flag_prebound) != ferr_ok) {
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

// exactly the same as x86_64 (for now)
ferr_t fint_register_special_handler(uint8_t number, fint_special_handler_f handler, void* data) {
	farch_int_special_handler_entry_t* entry;
	ferr_t status = ferr_temporary_outage;

	if (number > SPECIAL_HANDLERS_MAX || !handler) {
		return ferr_invalid_argument;
	}

	entry = &special_handlers[number];

	flock_spin_intsafe_lock(&entry->lock);

	if (!entry->handler) {
		status = ferr_ok;
		entry->handler = handler;
		entry->data = data;
	}

	flock_spin_intsafe_unlock(&entry->lock);

	return status;
};

void farch_int_set_lower_el_handler(farch_int_lower_el_handler_f handler) {
	flock_spin_intsafe_lock(&lower_el_handler_lock);
	lower_el_handler = handler;
	flock_spin_intsafe_unlock(&lower_el_handler_lock);
};
