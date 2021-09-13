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
 * AARCH64 implementations of architecture-specific functions for the scheduler subsystem.
 */

#include <ferro/core/scheduler.private.h>
#include <ferro/core/entry.h>
#include <ferro/core/panic.h>

void farch_sched_immediate_switch(fthread_saved_context_t* out_context, fthread_saved_context_t* new_context);
void farch_sched_delayed_switch(fthread_saved_context_t* new_context);
FERRO_NO_RETURN void farch_sched_bootstrap_switch(fthread_saved_context_t* new_context);

// used by our helpers
void farch_sched_set_interrupt_disable_count(uint64_t idc) {
	FARCH_PER_CPU(outstanding_interrupt_disable_count) = idc;
};

void fsched_switch(fthread_t* current_thread, fthread_t* new_thread) {
	// we don't want to be interrupted while we're switching
	fint_disable();

	if (fint_is_interrupt_context()) {
		fint_frame_t* frame = FARCH_PER_CPU(current_exception_frame);

		// save the current context
		if (current_thread) {
			current_thread->saved_context.x0                = frame->x0;
			current_thread->saved_context.x1                = frame->x1;
			current_thread->saved_context.x2                = frame->x2;
			current_thread->saved_context.x3                = frame->x3;
			current_thread->saved_context.x4                = frame->x4;
			current_thread->saved_context.x5                = frame->x5;
			current_thread->saved_context.x6                = frame->x6;
			current_thread->saved_context.x7                = frame->x7;
			current_thread->saved_context.x8                = frame->x8;
			current_thread->saved_context.x9                = frame->x9;
			current_thread->saved_context.x10               = frame->x10;
			current_thread->saved_context.x11               = frame->x11;
			current_thread->saved_context.x12               = frame->x12;
			current_thread->saved_context.x13               = frame->x13;
			current_thread->saved_context.x14               = frame->x14;
			current_thread->saved_context.x15               = frame->x15;
			current_thread->saved_context.x16               = frame->x16;
			current_thread->saved_context.x17               = frame->x17;
			current_thread->saved_context.x18               = frame->x18;
			current_thread->saved_context.x19               = frame->x19;
			current_thread->saved_context.x20               = frame->x20;
			current_thread->saved_context.x21               = frame->x21;
			current_thread->saved_context.x22               = frame->x22;
			current_thread->saved_context.x23               = frame->x23;
			current_thread->saved_context.x24               = frame->x24;
			current_thread->saved_context.x25               = frame->x25;
			current_thread->saved_context.x26               = frame->x26;
			current_thread->saved_context.x27               = frame->x27;
			current_thread->saved_context.x28               = frame->x28;
			current_thread->saved_context.x29               = frame->x29;
			current_thread->saved_context.x30               = frame->x30;
			current_thread->saved_context.pc                = frame->elr;
			current_thread->saved_context.sp                = frame->sp;
			current_thread->saved_context.pstate            = frame->pstate;
			current_thread->saved_context.interrupt_disable = frame->interrupt_disable;
		}

		// setup the frame to return to our helper
		frame->elr = (uintptr_t)farch_sched_delayed_switch;
		frame->x0 = (uintptr_t)&new_thread->saved_context;

		// make sure interrupts are disabled for our helper
		frame->pstate |= farch_thread_pstate_debug_mask | farch_thread_pstate_serror_mask | farch_thread_pstate_irq_mask | farch_thread_pstate_fiq_mask;
		frame->interrupt_disable = 1;

		FARCH_PER_CPU(current_thread) = new_thread;
	} else {
		if (current_thread) {
			// store the old interrupt-disable count
			current_thread->saved_context.interrupt_disable = FARCH_PER_CPU(outstanding_interrupt_disable_count);

			// store the pstate here and now, because on AARCH64 this cannot be simply read from a single register, so it's easier to do this here in C.
			// the processor state shouldn't change significantly between here and the switch point.
			{
				uint64_t current_el;
				uint64_t daif;
				uint64_t nzcv;
				uint64_t spsel;

				__asm__ volatile(
					"mrs %0, currentel\n"
					"mrs %1, daif\n"
					"mrs %2, nzcv\n"
					"mrs %3, spsel\n"
					:
					"=r" (current_el),
					"=r" (daif),
					"=r" (nzcv),
					"=r" (spsel)
				);

				// each register already has its bits set in the right place for the spsr
				current_thread->saved_context.pstate = nzcv | daif | current_el | spsel;
			}
		}

		FARCH_PER_CPU(current_thread) = new_thread;

		farch_sched_immediate_switch(current_thread ? &current_thread->saved_context : NULL, &new_thread->saved_context);
	}

	fint_enable();
};

FERRO_NO_RETURN void fsched_bootstrap(fthread_t* new_thread) {
	fint_disable();

	if (fint_is_interrupt_context()) {
		fpanic("fsched_bootstrap called from interrupt context");
	}

	FARCH_PER_CPU(current_thread) = new_thread;

	farch_sched_bootstrap_switch(&new_thread->saved_context);
};

void farch_sched_init(void) {};

void fsched_preempt_thread(fthread_t* thread) {
	if (thread == fthread_current()) {
		// first disarm the timer
		fsched_disarm_timer();

		// now trigger the auxillary interrupt
		// (the threading subsystem's interrupt hooks will take care of the rest)
		__asm__ volatile("svc \0430xfffe");
	} else {
		fpanic("Yielding thread is not current thread (this is impossible in the current non-SMP implementation)");
	}
};
