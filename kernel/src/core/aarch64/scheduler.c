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
#include <ferro/core/paging.h>
#include <libsimple/libsimple.h>

// 4 pages should be enough, right?
#define SWITCHING_STACK_SIZE (FPAGE_PAGE_SIZE * 4)

void farch_sched_immediate_switch(fthread_saved_context_t* out_context, fthread_saved_context_t* new_context);
void farch_sched_delayed_switch(fthread_saved_context_t* new_context);
FERRO_NO_RETURN void farch_sched_bootstrap_switch(fthread_saved_context_t* new_context);

//
// DEBUGGING
//

#include <ferro/core/console.h>

void farch_sched_dump_context(fthread_saved_context_t* saved_context) {
	fconsole_logf(
		"x0=%llu,x1=%llu\n"
		"x2=%llu,x3=%llu\n"
		"x4=%llu,x5=%llu\n"
		"x6=%llu,x7=%llu\n"
		"x8=%llu,x9=%llu\n"
		"x10=%llu,x11=%llu\n"
		"x12=%llu,x13=%llu\n"
		"x14=%llu,x15=%llu\n"
		"x16=%llu,x17=%llu\n"
		"x18=%llu,x19=%llu\n"
		"x20=%llu,x21=%llu\n"
		"x22=%llu,x23=%llu\n"
		"x24=%llu,x25=%llu\n"
		"x26=%llu,x27=%llu\n"
		"x28=%llu,x29=%llu\n"
		"x30=%llu,pc=%llu\n"
		"sp=%llu,pstate=%llu\n"
		"interrupt_disable=%llu\n"
		"address_space=%llu\n",
		saved_context->x0,  saved_context->x1,
		saved_context->x2,  saved_context->x3,
		saved_context->x4,  saved_context->x5,
		saved_context->x6,  saved_context->x7,
		saved_context->x8,  saved_context->x9,
		saved_context->x10, saved_context->x11,
		saved_context->x12, saved_context->x13,
		saved_context->x14, saved_context->x15,
		saved_context->x16, saved_context->x17,
		saved_context->x18, saved_context->x19,
		saved_context->x20, saved_context->x21,
		saved_context->x22, saved_context->x23,
		saved_context->x24, saved_context->x25,
		saved_context->x26, saved_context->x27,
		saved_context->x28, saved_context->x29,
		saved_context->x30, saved_context->pc,
		saved_context->sp,  saved_context->pstate,
		saved_context->interrupt_disable,
		saved_context->address_space
	);
};

void fsched_switch(fthread_t* current_thread, fthread_t* new_thread) {
	// we don't want to be interrupted while we're switching
	fint_disable();

	if (fint_is_interrupt_context()) {
		fint_frame_t* frame = FARCH_PER_CPU(current_exception_frame);
		fthread_saved_context_t* saved = NULL;

		// save the current context
		//
		// note that we do NOT save the old frame data to the current thread if the frame
		// has already been set up as the switching frame. if the frame has already been set up
		// as the switching frame. that means that the data in the current thread's saved context
		// is already up-to-date (it's either been freshly switched from or we we're going to switch to it)
		if (current_thread && frame->elr != farch_sched_delayed_switch) {
			current_thread->saved_context->x0                = frame->x0;
			current_thread->saved_context->x1                = frame->x1;
			current_thread->saved_context->x2                = frame->x2;
			current_thread->saved_context->x3                = frame->x3;
			current_thread->saved_context->x4                = frame->x4;
			current_thread->saved_context->x5                = frame->x5;
			current_thread->saved_context->x6                = frame->x6;
			current_thread->saved_context->x7                = frame->x7;
			current_thread->saved_context->x8                = frame->x8;
			current_thread->saved_context->x9                = frame->x9;
			current_thread->saved_context->x10               = frame->x10;
			current_thread->saved_context->x11               = frame->x11;
			current_thread->saved_context->x12               = frame->x12;
			current_thread->saved_context->x13               = frame->x13;
			current_thread->saved_context->x14               = frame->x14;
			current_thread->saved_context->x15               = frame->x15;
			current_thread->saved_context->x16               = frame->x16;
			current_thread->saved_context->x17               = frame->x17;
			current_thread->saved_context->x18               = frame->x18;
			current_thread->saved_context->x19               = frame->x19;
			current_thread->saved_context->x20               = frame->x20;
			current_thread->saved_context->x21               = frame->x21;
			current_thread->saved_context->x22               = frame->x22;
			current_thread->saved_context->x23               = frame->x23;
			current_thread->saved_context->x24               = frame->x24;
			current_thread->saved_context->x25               = frame->x25;
			current_thread->saved_context->x26               = frame->x26;
			current_thread->saved_context->x27               = frame->x27;
			current_thread->saved_context->x28               = frame->x28;
			current_thread->saved_context->x29               = frame->x29;
			current_thread->saved_context->x30               = frame->x30;
			current_thread->saved_context->pc                = frame->elr;
			current_thread->saved_context->sp                = frame->sp;
			current_thread->saved_context->pstate            = frame->pstate;
			current_thread->saved_context->interrupt_disable = frame->interrupt_disable;
			current_thread->saved_context->address_space     = frame->address_space;
			current_thread->saved_context->fpsr              = frame->fpsr;
			current_thread->saved_context->fpcr              = frame->fpcr;
			simple_memcpy(current_thread->saved_context->fp_registers, frame->fp_registers, sizeof(current_thread->saved_context->fp_registers));
		}

		// setup the switching context
		// use the switching stack
		saved = (void*)((char*)FARCH_PER_CPU(switching_stack) - sizeof(fthread_saved_context_t));
		simple_memcpy(saved, new_thread->saved_context, sizeof(*saved));

		//fconsole_log("doing delayed switch to:\n");
		//farch_sched_dump_context(saved);

		// setup the frame to return to our helper
		frame->elr = (uintptr_t)farch_sched_delayed_switch;
		frame->x0 = (uintptr_t)saved;

		// make sure interrupts are disabled for our helper and make sure we run it in EL1 with SP_EL0 under AARCH64 (not AARCH32)
		// the helper will change the PSTATE as necessary when it perform a fake exception return
		frame->pstate = farch_thread_pstate_debug_mask | farch_thread_pstate_serror_mask | farch_thread_pstate_irq_mask | farch_thread_pstate_fiq_mask | farch_thread_pstate_el1 | farch_thread_pstate_sp0 | farch_thread_pstate_aarch64;
		frame->interrupt_disable = 1;

		frame->sp = (uintptr_t)saved;

		// the new address space is loaded by the interrupt handler (not our helper)
		frame->address_space = new_thread->saved_context->address_space;

		FARCH_PER_CPU(current_thread) = new_thread;
	} else {
		if (current_thread) {
			// store the old interrupt-disable count
			current_thread->saved_context->interrupt_disable = FARCH_PER_CPU(outstanding_interrupt_disable_count);

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
				current_thread->saved_context->pstate = nzcv | daif | current_el | spsel;
			}

			// save the old address space
			current_thread->saved_context->address_space = (uintptr_t)FARCH_PER_CPU(address_space);
		}

		// swap in the new address space here (it's easier)
		fpanic_status(fpage_space_swap((void*)new_thread->saved_context->address_space));

		FARCH_PER_CPU(current_thread) = new_thread;

		farch_sched_immediate_switch(current_thread ? current_thread->saved_context : NULL, new_thread->saved_context);
	}

	fint_enable();
};

FERRO_NO_RETURN void fsched_bootstrap(fthread_t* new_thread) {
	fint_disable();

	if (fint_is_interrupt_context()) {
		fpanic("fsched_bootstrap called from interrupt context");
	}

	// swap in the new address space here (it's easier)
	fpanic_status(fpage_space_swap((void*)new_thread->saved_context->address_space));

	FARCH_PER_CPU(current_thread) = new_thread;

	farch_sched_bootstrap_switch(new_thread->saved_context);
};

void farch_sched_init(void) {
	if (fpage_allocate_kernel(fpage_round_up_to_page_count(SWITCHING_STACK_SIZE), &FARCH_PER_CPU(switching_stack), 0) != ferr_ok) {
		fpanic("Failed to allocate a switching stack");
	}

	FARCH_PER_CPU(switching_stack) = (void*)((uintptr_t)FARCH_PER_CPU(switching_stack) + SWITCHING_STACK_SIZE);
};

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
