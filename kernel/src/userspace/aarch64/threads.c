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

#include <ferro/userspace/threads.private.h>
#include <ferro/core/interrupts.h>
#include <libsimple/libsimple.h>
#include <ferro/core/panic.h>
#include <ferro/core/console.h>

// we make `tpidr_el0` a kernel-managed register (i.e. we require a syscall in order to modify it)
// even though userspace can modify it freely according to the architecture. we do this is because
// we want to avoid storing it on every context switch.
//
// TODO: maybe change this and just eat the cost of saving the register every context switch.

FERRO_NO_RETURN void farch_uthread_jump_user_frame(void* pc, void* sp);
FERRO_NO_RETURN void farch_uthread_return_to_userspace(fthread_saved_context_t* saved_syscall_context);

void futhread_jump_user_self_arch(fthread_t* uthread, futhread_data_t* udata, void* address) {
	futhread_data_private_t* private_data = (void*)udata;

	// disable interrupts so we can jump safely
	fint_disable();

	// load up TPIDR_EL0 here
	__asm__ volatile("msr tpidr_el0, %0" :: "r" (private_data->arch.tpidr_el0));

	// now jump into userspace
	farch_uthread_jump_user_frame(address, (char*)udata->user_stack_base + udata->user_stack_size);
};

void futhread_ending_interrupt_arch(fthread_t* uthread, futhread_data_t* udata) {
	futhread_data_private_t* private_data = (void*)udata;
	FARCH_PER_CPU(current_uthread_data) = udata;
	__asm__ volatile("msr tpidr_el0, %0" :: "r" (private_data->arch.tpidr_el0));
};

FERRO_NO_RETURN
static void farch_uthread_syscall_wrapper(void) {
	futhread_data_private_t* private_data = (void*)FARCH_PER_CPU(current_uthread_data);

	// load in the address space
	fpanic_status(fpage_space_swap((void*)FARCH_PER_CPU(current_uthread_data)->saved_syscall_context->address_space));

	// we know that, coming from userspace, we have no reason to be marked as interrupted;
	// any possible signals will be checked in a moment anyways. the only time we care about the
	// thread interrupt flag is *during* a syscall, since it lets us know that we should exit early.
	fthread_unmark_interrupted(FARCH_PER_CPU(current_thread));

	if (futhread_handle_signals(FARCH_PER_CPU(current_thread), false) != ferr_signaled) {
		if (FARCH_PER_CPU(current_uthread_data)->syscall_handler) {
			FARCH_PER_CPU(current_uthread_data)->syscall_handler(FARCH_PER_CPU(current_uthread_data)->syscall_handler_context, FARCH_PER_CPU(current_thread), FARCH_PER_CPU(current_uthread_data)->saved_syscall_context);
		} else {
			// TODO: indicate that the thread is dying from an error
			fthread_kill_self();
		}
	}

	flock_mutex_lock(&private_data->signals_mutex);

	// if there are signals to handle, it'll set them up to be handled upon return to userspace.
	FERRO_WUR_IGNORE(futhread_handle_signals(FARCH_PER_CPU(current_thread), true));

	// disable interrupts so we can return safely
	fint_disable();

	// we unlock this with interrupts disabled to avoid a race if someone else signals us with a preemptive signal
	// and sees that we're in kernel-space. if they see we're in kernel-space, they just queue the preemptive signal.
	// if we unlocked this with interrupts enabled, someone might signal us in the time between the check we just did and
	// the interrupt-disable.
	flock_mutex_unlock(&private_data->signals_mutex);

	// we can also unmark the thread as interrupted here.
	// we know that if someone set the "interrupted" flag, that's because
	// a signal was pending, which we've already handled.
	fthread_unmark_interrupted(FARCH_PER_CPU(current_thread));

	if (private_data->use_fake_interrupt_return) {
		private_data->use_fake_interrupt_return = false;
		// on AARCH64, the syscall mechanism doesn't clobber any registers, so we don't need to do anything different in this case.
		// actually, on AARCH64, syscalls are *always* exited with a fake exception return; that's the only way to do it.
	}

	farch_uthread_return_to_userspace(FARCH_PER_CPU(current_uthread_data)->saved_syscall_context);
};

static void farch_uthread_handle_lower_el_sync(fint_frame_t* frame, farch_int_esr_code_t code, uint32_t iss) {
	switch (code) {
		case farch_int_esr_code_svc64: {
			if (iss == 0) {
				fthread_t* current_thread = FARCH_PER_CPU(current_thread);
				futhread_data_t* current_uthread_data = FARCH_PER_CPU(current_uthread_data);
				fthread_saved_context_t* saved_context = current_uthread_data->saved_syscall_context;

				// save the state into the uthread context
				saved_context->x0  = frame->x0 ;
				saved_context->x1  = frame->x1 ;
				saved_context->x2  = frame->x2 ;
				saved_context->x3  = frame->x3 ;
				saved_context->x4  = frame->x4 ;
				saved_context->x5  = frame->x5 ;
				saved_context->x6  = frame->x6 ;
				saved_context->x7  = frame->x7 ;
				saved_context->x8  = frame->x8 ;
				saved_context->x9  = frame->x9 ;
				saved_context->x10 = frame->x10;
				saved_context->x11 = frame->x11;
				saved_context->x12 = frame->x12;
				saved_context->x13 = frame->x13;
				saved_context->x14 = frame->x14;
				saved_context->x15 = frame->x15;
				saved_context->x16 = frame->x16;
				saved_context->x17 = frame->x17;
				saved_context->x18 = frame->x18;
				saved_context->x19 = frame->x19;
				saved_context->x20 = frame->x20;
				saved_context->x21 = frame->x21;
				saved_context->x22 = frame->x22;
				saved_context->x23 = frame->x23;
				saved_context->x24 = frame->x24;
				saved_context->x25 = frame->x25;
				saved_context->x26 = frame->x26;
				saved_context->x27 = frame->x27;
				saved_context->x28 = frame->x28;
				saved_context->x29 = frame->x29;
				saved_context->x30 = frame->x30;
				saved_context->pc = frame->elr;
				saved_context->sp = frame->sp;
				saved_context->pstate = frame->pstate;
				saved_context->interrupt_disable = frame->interrupt_disable;
				saved_context->address_space = frame->address_space;
				saved_context->fpsr = frame->fpsr;
				saved_context->fpcr = frame->fpcr;
				simple_memcpy(saved_context->fp_registers, frame->fp_registers, sizeof(saved_context->fp_registers));

				// now set up the frame so we can perform the syscall when we return from this exception
				simple_memset(frame, 0, sizeof(*frame));
				frame->elr = (uintptr_t)farch_uthread_syscall_wrapper;
				frame->sp = (uintptr_t)current_thread->stack_base + current_thread->stack_size;
				frame->pstate = farch_thread_pstate_aarch64 | farch_thread_pstate_el1 | farch_thread_pstate_sp0;
				frame->address_space = saved_context->address_space;
			} else {
				fpanic("bad SVC number %u", iss);
			}
		} break;

		case farch_int_esr_code_instruction_abort_lower_el: {
			if (!farch_int_invoke_special_handler(fint_special_interrupt_page_fault)) {
				fconsole_logf("instruction abort from lower el at %p on address %p\n", (void*)frame->elr, (void*)frame->far);
				farch_int_print_frame(frame);
				fpanic("instruction abort in userspace");
			}
		} break;

		case farch_int_esr_code_data_abort_lower_el: {
			if (!farch_int_invoke_special_handler(fint_special_interrupt_page_fault)) {
				fconsole_logf("data abort from lower el at %p on address %p\n", (void*)frame->elr, (void*)frame->far);
				farch_int_print_frame(frame);
				fpanic("data abort in userspace");
			}
		} break;

		case farch_int_esr_code_brk:
		case farch_int_esr_code_breakpoint_lower_el: {
			fconsole_logf("breakpoint from lower el at %p\n", (void*)frame->elr);
			frame->elr += 4;
		} break;

		case farch_int_esr_code_software_step_lower_el: {
			fconsole_logf("software step from lower el at %p\n", (void*)frame->elr);
			frame->elr += 4;
		} break;

		case farch_int_esr_code_watchpoint_lower_el: {
			fconsole_logf("watchpoint hit from lower el at %p on address %p\n", (void*)frame->elr, (void*)frame->far);
			frame->elr += 4;
		} break;

		default:
			fint_trace_interrupted_stack(fint_current_frame());
			fpanic("invalid synchronous exception from lower el: %u; generated at %p", code, (void*)frame->elr);
	}
};

void futhread_arch_init(void) {
	farch_int_set_lower_el_handler(farch_uthread_handle_lower_el_sync);
};

void futhread_arch_init_private_data(futhread_data_private_t* data) {
	data->arch.tpidr_el0 = 0;
};
