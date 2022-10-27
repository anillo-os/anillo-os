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
 * x86_64 implementations of architecture-specific functions for the scheduler subsystem.
 */

#include <ferro/core/x86_64/scheduler.private.h>
#include <ferro/core/interrupts.h>
#include <ferro/core/panic.h>
#include <ferro/core/mempool.h>
#include <libsimple/libsimple.h>
#include <ferro/core/paging.h>

// 4 pages should be enough, right?
#define SWITCHING_STACK_SIZE (FPAGE_PAGE_SIZE * 4)

static void ignore_interrupt(void* data, fint_frame_t* frame) {};

void farch_sched_init(void) {
	if (farch_int_register_handler(0xfe, ignore_interrupt, NULL) != ferr_ok) {
		fpanic("Failed to register scheduler auxillary interrupt");
	}

	if (fpage_allocate_kernel(fpage_round_up_to_page_count(SWITCHING_STACK_SIZE), &FARCH_PER_CPU(switching_stack), 0) != ferr_ok) {
		fpanic("Failed to allocate a switching stack");
	}

	FARCH_PER_CPU(switching_stack) = (void*)((uintptr_t)FARCH_PER_CPU(switching_stack) + SWITCHING_STACK_SIZE);
};

void farch_sched_immediate_switch(fthread_saved_context_t* out_context, fint_frame_t* new_frame);
void farch_sched_delayed_switch(fint_frame_t* new_frame);
FERRO_NO_RETURN void farch_sched_bootstrap_switch(fint_frame_t* new_frame);

// used by our helpers
void farch_sched_set_interrupt_disable_count(uint64_t idc) {
	FARCH_PER_CPU(outstanding_interrupt_disable_count) = idc;
};

void fsched_switch(fthread_t* current_thread, fthread_t* new_thread) {
	// we don't want to be interrupted while we're switching
	fint_disable();

	if (fint_is_interrupt_context()) {
		fint_frame_t* frame = FARCH_PER_CPU(current_exception_frame);
		fint_frame_t* new_frame;
		void* xsave_area;

		// okay, so we're currently in an interrupt. well, we don't want to switch here,
		// so we'll set everything up to switch later, after we return from the interrupt
		//
		// to do this, we modify the return frame to load our own helper (farch_sched_delayed_switch())

		if (current_thread) {
			// first, save the old frame data to the current thread
			current_thread->saved_context->rax    = frame->saved_registers.rax;
			current_thread->saved_context->rcx    = frame->saved_registers.rcx;
			current_thread->saved_context->rdx    = frame->saved_registers.rdx;
			current_thread->saved_context->rbx    = frame->saved_registers.rbx;
			current_thread->saved_context->rsi    = frame->saved_registers.rsi;
			current_thread->saved_context->rdi    = frame->saved_registers.rdi;
			current_thread->saved_context->rsp    = (uintptr_t)frame->core.rsp;
			current_thread->saved_context->rbp    = frame->saved_registers.rbp;
			current_thread->saved_context->r8     = frame->saved_registers.r8;
			current_thread->saved_context->r9     = frame->saved_registers.r9;
			current_thread->saved_context->r10    = frame->saved_registers.r10;
			current_thread->saved_context->r11    = frame->saved_registers.r11;
			current_thread->saved_context->r12    = frame->saved_registers.r12;
			current_thread->saved_context->r13    = frame->saved_registers.r13;
			current_thread->saved_context->r14    = frame->saved_registers.r14;
			current_thread->saved_context->r15    = frame->saved_registers.r15;
			current_thread->saved_context->rip    = (uintptr_t)frame->core.rip;
			current_thread->saved_context->rflags = frame->core.rflags;
			current_thread->saved_context->cs     = frame->core.cs;
			current_thread->saved_context->ss     = frame->core.ss;
			current_thread->saved_context->interrupt_disable = frame->saved_registers.interrupt_disable;
			current_thread->saved_context->address_space = frame->saved_registers.address_space;

			simple_memcpy(&current_thread->saved_context->xsave_area[0], frame->xsave_area, FARCH_PER_CPU(xsave_area_size));
		}

		// NOTE: we use a temporary stack (the switching stack) for context switching because we cannot use the target thread's stack since it may have a red zone
		//       (and pushing our data onto it would corrupt the red zone)

		// determine the address of the XSAVE area on the switching stack
		xsave_area = (void*)fpage_align_address_down((uintptr_t)FARCH_PER_CPU(switching_stack) - FARCH_PER_CPU(xsave_area_size), fpage_round_down_to_alignment_power(64));

		// load the new frame data onto the switching stack
		new_frame = (void*)((uintptr_t)xsave_area - sizeof(*new_frame));
		new_frame->saved_registers.rax = new_thread->saved_context->rax;
		new_frame->saved_registers.rcx = new_thread->saved_context->rcx;
		new_frame->saved_registers.rdx = new_thread->saved_context->rdx;
		new_frame->saved_registers.rbx = new_thread->saved_context->rbx;
		new_frame->saved_registers.rsi = new_thread->saved_context->rsi;
		new_frame->saved_registers.rdi = new_thread->saved_context->rdi;
		new_frame->core.rsp            = (void*)new_thread->saved_context->rsp;
		new_frame->saved_registers.rbp = new_thread->saved_context->rbp;
		new_frame->saved_registers.r8  = new_thread->saved_context->r8;
		new_frame->saved_registers.r9  = new_thread->saved_context->r9;
		new_frame->saved_registers.r10 = new_thread->saved_context->r10;
		new_frame->saved_registers.r11 = new_thread->saved_context->r11;
		new_frame->saved_registers.r12 = new_thread->saved_context->r12;
		new_frame->saved_registers.r13 = new_thread->saved_context->r13;
		new_frame->saved_registers.r14 = new_thread->saved_context->r14;
		new_frame->saved_registers.r15 = new_thread->saved_context->r15;
		new_frame->core.rip            = (void*)new_thread->saved_context->rip;
		new_frame->core.rflags         = new_thread->saved_context->rflags;
		new_frame->core.cs             = new_thread->saved_context->cs;
		new_frame->core.ss             = new_thread->saved_context->ss;

		// set up the XSAVE area in the new frame
		new_frame->xsave_area = xsave_area;
		simple_memcpy(new_frame->xsave_area, &new_thread->saved_context->xsave_area[0], FARCH_PER_CPU(xsave_area_size));

		// interrupt-disable is loaded later, by our helper
		new_frame->saved_registers.interrupt_disable = new_thread->saved_context->interrupt_disable;

		// finally, set up the return frame to load our helper function
		frame->core.rip = farch_sched_delayed_switch;
		frame->core.rsp = new_frame;
		frame->saved_registers.rdi = (uintptr_t)new_frame;

		// make sure interrupts are disabled for the helper
		frame->core.rflags &= ~(1ULL << 9);
		frame->saved_registers.interrupt_disable = 1;

		// also make sure we stay in kernel-space
		// the helper will switch to userspace (if necessary) via the fake interrupt return
		frame->core.cs = farch_int_gdt_index_code * 8;
		frame->core.ss = farch_int_gdt_index_data * 8;

		// the new address space is loaded by the interrupt handler (not our helper)
		frame->saved_registers.address_space = new_thread->saved_context->address_space;
		new_frame->saved_registers.address_space = new_thread->saved_context->address_space;

		FARCH_PER_CPU(current_thread) = new_thread;

		// and that's it; we'll let the interrupt handler take care of the rest

		// hopefully the interrupt handler won't dilly-dally for too long
		// (but since we arm the timer once we return, it won't affect the new thread's time slice)
	} else {
		fint_frame_t frame;
		char xsave_area[FARCH_PER_CPU(xsave_area_size)] __attribute__((aligned(64)));

		frame.xsave_area = &xsave_area[0];

		frame.saved_registers.rax = new_thread->saved_context->rax;
		frame.saved_registers.rcx = new_thread->saved_context->rcx;
		frame.saved_registers.rdx = new_thread->saved_context->rdx;
		frame.saved_registers.rbx = new_thread->saved_context->rbx;
		frame.saved_registers.rsi = new_thread->saved_context->rsi;
		frame.saved_registers.rdi = new_thread->saved_context->rdi;
		frame.core.rsp            = (void*)new_thread->saved_context->rsp;
		frame.saved_registers.rbp = new_thread->saved_context->rbp;
		frame.saved_registers.r8  = new_thread->saved_context->r8;
		frame.saved_registers.r9  = new_thread->saved_context->r9;
		frame.saved_registers.r10 = new_thread->saved_context->r10;
		frame.saved_registers.r11 = new_thread->saved_context->r11;
		frame.saved_registers.r12 = new_thread->saved_context->r12;
		frame.saved_registers.r13 = new_thread->saved_context->r13;
		frame.saved_registers.r14 = new_thread->saved_context->r14;
		frame.saved_registers.r15 = new_thread->saved_context->r15;
		frame.core.rip            = (void*)new_thread->saved_context->rip;
		frame.core.rflags         = new_thread->saved_context->rflags;
		frame.core.cs             = new_thread->saved_context->cs;
		frame.core.ss             = new_thread->saved_context->ss;
		frame.saved_registers.interrupt_disable = new_thread->saved_context->interrupt_disable;
		frame.saved_registers.address_space = new_thread->saved_context->address_space;

		simple_memcpy(frame.xsave_area, &new_thread->saved_context->xsave_area[0], FARCH_PER_CPU(xsave_area_size));

		if (current_thread) {
			// store the old interrupt-disable count
			current_thread->saved_context->interrupt_disable = FARCH_PER_CPU(outstanding_interrupt_disable_count);
			// and the old address space
			current_thread->saved_context->address_space = (uintptr_t)FARCH_PER_CPU(address_space);
		}

		// swap in the new address space here (it's easier)
		fpanic_status(fpage_space_swap((void*)new_thread->saved_context->address_space));

		FARCH_PER_CPU(current_thread) = new_thread;

		farch_sched_immediate_switch(current_thread ? current_thread->saved_context : NULL, &frame);
	}

	fint_enable();
};

FERRO_NO_RETURN void fsched_bootstrap(fthread_t* new_thread) {
	fint_frame_t frame;

	char xsave_area[FARCH_PER_CPU(xsave_area_size)] __attribute__((aligned(64)));

	frame.xsave_area = &xsave_area[0];

	fint_disable();

	if (fint_is_interrupt_context()) {
		fpanic("fsched_bootstrap called from interrupt context");
	}

	frame.saved_registers.rax = new_thread->saved_context->rax;
	frame.saved_registers.rcx = new_thread->saved_context->rcx;
	frame.saved_registers.rdx = new_thread->saved_context->rdx;
	frame.saved_registers.rbx = new_thread->saved_context->rbx;
	frame.saved_registers.rsi = new_thread->saved_context->rsi;
	frame.saved_registers.rdi = new_thread->saved_context->rdi;
	frame.core.rsp            = (void*)new_thread->saved_context->rsp;
	frame.saved_registers.rbp = new_thread->saved_context->rbp;
	frame.saved_registers.r8  = new_thread->saved_context->r8;
	frame.saved_registers.r9  = new_thread->saved_context->r9;
	frame.saved_registers.r10 = new_thread->saved_context->r10;
	frame.saved_registers.r11 = new_thread->saved_context->r11;
	frame.saved_registers.r12 = new_thread->saved_context->r12;
	frame.saved_registers.r13 = new_thread->saved_context->r13;
	frame.saved_registers.r14 = new_thread->saved_context->r14;
	frame.saved_registers.r15 = new_thread->saved_context->r15;
	frame.core.rip            = (void*)new_thread->saved_context->rip;
	frame.core.rflags         = new_thread->saved_context->rflags;
	frame.core.cs             = new_thread->saved_context->cs;
	frame.core.ss             = new_thread->saved_context->ss;
	frame.saved_registers.interrupt_disable = new_thread->saved_context->interrupt_disable;
	frame.saved_registers.address_space = new_thread->saved_context->address_space;

	simple_memcpy(frame.xsave_area, &new_thread->saved_context->xsave_area[0], FARCH_PER_CPU(xsave_area_size));

	// swap in the new address space here (it's easier)
	fpanic_status(fpage_space_swap((void*)new_thread->saved_context->address_space));

	FARCH_PER_CPU(current_thread) = new_thread;

	farch_sched_bootstrap_switch(&frame);
};

void fsched_preempt_thread(fthread_t* thread) {
	if (thread == fthread_current()) {
		// first disarm the timer
		fsched_disarm_timer();

		// now trigger the auxillary interrupt
		// (the threading subsystem's interrupt hooks will take care of the rest)
		__asm__ volatile("int $0xfe");
	} else {
		fpanic("Yielding thread is not current thread (this is impossible in the current non-SMP implementation)");
	}
};
