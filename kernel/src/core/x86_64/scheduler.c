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
#include <ferro/core/x86_64/interrupts.h>
#include <ferro/core/panic.h>
#include <ferro/core/mempool.h>
#include <libk/libk.h>

static void ignore_interrupt(farch_int_isr_frame_t* frame) {};

void farch_sched_init(void) {
	if (farch_int_register_handler(0xfe, ignore_interrupt) != ferr_ok) {
		fpanic("Failed to register scheduler auxillary interrupt");
	}
};

void farch_sched_immediate_switch(fthread_saved_context_t* out_context, farch_int_isr_frame_t* new_frame);
void farch_sched_delayed_switch(farch_int_isr_frame_t* new_frame);
FERRO_NO_RETURN void farch_sched_bootstrap_switch(farch_int_isr_frame_t* new_frame);

// used by our helpers
void farch_sched_set_interrupt_disable_count(uint64_t idc) {
	FARCH_PER_CPU(outstanding_interrupt_disable_count) = idc;
};

void fsched_switch(fthread_t* current_thread, fthread_t* new_thread) {
	// we don't want to be interrupted while we're switching
	fint_disable();

	if (fint_is_interrupt_context()) {
		farch_int_isr_frame_t* frame = FARCH_PER_CPU(current_exception_frame);
		farch_int_isr_frame_t* new_frame;

		// okay, so we're currently in an interrupt. well, we don't want to switch here,
		// so we'll set everything up to switch later, after we return from the interrupt
		//
		// to do this, we modify the return frame to load our own helper (farch_sched_delayed_switch())

		if (current_thread) {
			// first, save the old frame data to the current thread
			current_thread->saved_context.rax    = frame->saved_registers.rax;
			current_thread->saved_context.rcx    = frame->saved_registers.rcx;
			current_thread->saved_context.rdx    = frame->saved_registers.rdx;
			current_thread->saved_context.rbx    = frame->saved_registers.rbx;
			current_thread->saved_context.rsi    = frame->saved_registers.rsi;
			current_thread->saved_context.rdi    = frame->saved_registers.rdi;
			current_thread->saved_context.rsp    = (uintptr_t)frame->rsp;
			current_thread->saved_context.rbp    = frame->saved_registers.rbp;
			current_thread->saved_context.r8     = frame->saved_registers.r8;
			current_thread->saved_context.r9     = frame->saved_registers.r9;
			current_thread->saved_context.r10    = frame->saved_registers.r10;
			current_thread->saved_context.r11    = frame->saved_registers.r11;
			current_thread->saved_context.r12    = frame->saved_registers.r12;
			current_thread->saved_context.r13    = frame->saved_registers.r13;
			current_thread->saved_context.r14    = frame->saved_registers.r14;
			current_thread->saved_context.r15    = frame->saved_registers.r15;
			current_thread->saved_context.rip    = (uintptr_t)frame->rip;
			current_thread->saved_context.rflags = frame->rflags;
			current_thread->saved_context.cs     = frame->cs;
			current_thread->saved_context.ss     = frame->ss;
			current_thread->saved_context.interrupt_disable = frame->saved_registers.interrupt_disable;
		}

		// NOTE: a very important detail regarding the safety of the following operations is that on x86, all interrupt handlers have their own stacks (and each CPU has its own set of interrupt stacks).
		//       therefore, the following will NOT overwrite the exception frame stored on the current interupt handler's stack, even if we're switching to the same thread.

		// load the new frame data onto the return stack
		new_frame = (void*)((uintptr_t)new_thread->saved_context.rsp - sizeof(*new_frame));
		new_frame->saved_registers.rax = new_thread->saved_context.rax;
		new_frame->saved_registers.rcx = new_thread->saved_context.rcx;
		new_frame->saved_registers.rdx = new_thread->saved_context.rdx;
		new_frame->saved_registers.rbx = new_thread->saved_context.rbx;
		new_frame->saved_registers.rsi = new_thread->saved_context.rsi;
		new_frame->saved_registers.rdi = new_thread->saved_context.rdi;
		new_frame->rsp                 = (void*)new_thread->saved_context.rsp;
		new_frame->saved_registers.rbp = new_thread->saved_context.rbp;
		new_frame->saved_registers.r8  = new_thread->saved_context.r8;
		new_frame->saved_registers.r9  = new_thread->saved_context.r9;
		new_frame->saved_registers.r10 = new_thread->saved_context.r10;
		new_frame->saved_registers.r11 = new_thread->saved_context.r11;
		new_frame->saved_registers.r12 = new_thread->saved_context.r12;
		new_frame->saved_registers.r13 = new_thread->saved_context.r13;
		new_frame->saved_registers.r14 = new_thread->saved_context.r14;
		new_frame->saved_registers.r15 = new_thread->saved_context.r15;
		new_frame->rip                 = (void*)new_thread->saved_context.rip;
		new_frame->rflags              = new_thread->saved_context.rflags;
		new_frame->cs                  = new_thread->saved_context.cs;
		new_frame->ss                  = new_thread->saved_context.ss;

		// interrupt-disable is loaded later, by our helper
		new_frame->saved_registers.interrupt_disable = new_thread->saved_context.interrupt_disable;

		// finally, set up the return frame to load our helper function
		frame->rip = farch_sched_delayed_switch;
		frame->rsp = new_frame;
		frame->saved_registers.rdi = (uintptr_t)new_frame;

		// make sure interrupts are disabled for the helper
		frame->rflags &= ~(1ULL << 9);
		frame->saved_registers.interrupt_disable = 1;

		FARCH_PER_CPU(current_thread) = new_thread;

		// and that's it; we'll let the interrupt handler take care of the rest

		// hopefully the interrupt handler won't dilly-dally for too long
		// (but since we arm the timer once we return, it won't affect the new thread's time slice)
	} else {
		farch_int_isr_frame_t frame;

		frame.saved_registers.rax = new_thread->saved_context.rax;
		frame.saved_registers.rcx = new_thread->saved_context.rcx;
		frame.saved_registers.rdx = new_thread->saved_context.rdx;
		frame.saved_registers.rbx = new_thread->saved_context.rbx;
		frame.saved_registers.rsi = new_thread->saved_context.rsi;
		frame.saved_registers.rdi = new_thread->saved_context.rdi;
		frame.rsp                 = (void*)new_thread->saved_context.rsp;
		frame.saved_registers.rbp = new_thread->saved_context.rbp;
		frame.saved_registers.r8  = new_thread->saved_context.r8;
		frame.saved_registers.r9  = new_thread->saved_context.r9;
		frame.saved_registers.r10 = new_thread->saved_context.r10;
		frame.saved_registers.r11 = new_thread->saved_context.r11;
		frame.saved_registers.r12 = new_thread->saved_context.r12;
		frame.saved_registers.r13 = new_thread->saved_context.r13;
		frame.saved_registers.r14 = new_thread->saved_context.r14;
		frame.saved_registers.r15 = new_thread->saved_context.r15;
		frame.rip                 = (void*)new_thread->saved_context.rip;
		frame.rflags              = new_thread->saved_context.rflags;
		frame.cs                  = new_thread->saved_context.cs;
		frame.ss                  = new_thread->saved_context.ss;
		frame.saved_registers.interrupt_disable = new_thread->saved_context.interrupt_disable;

		// store the old interrupt-disable count
		if (current_thread) {
			current_thread->saved_context.interrupt_disable = FARCH_PER_CPU(outstanding_interrupt_disable_count);
		}

		FARCH_PER_CPU(current_thread) = new_thread;

		farch_sched_immediate_switch(current_thread ? &current_thread->saved_context : NULL, &frame);
	}

	fint_enable();
};

FERRO_NO_RETURN void fsched_bootstrap(fthread_t* new_thread) {
	farch_int_isr_frame_t frame;

	fint_disable();

	if (fint_is_interrupt_context()) {
		fpanic("fsched_bootstrap called from interrupt context");
	}

	frame.saved_registers.rax = new_thread->saved_context.rax;
	frame.saved_registers.rcx = new_thread->saved_context.rcx;
	frame.saved_registers.rdx = new_thread->saved_context.rdx;
	frame.saved_registers.rbx = new_thread->saved_context.rbx;
	frame.saved_registers.rsi = new_thread->saved_context.rsi;
	frame.saved_registers.rdi = new_thread->saved_context.rdi;
	frame.rsp                 = (void*)new_thread->saved_context.rsp;
	frame.saved_registers.rbp = new_thread->saved_context.rbp;
	frame.saved_registers.r8  = new_thread->saved_context.r8;
	frame.saved_registers.r9  = new_thread->saved_context.r9;
	frame.saved_registers.r10 = new_thread->saved_context.r10;
	frame.saved_registers.r11 = new_thread->saved_context.r11;
	frame.saved_registers.r12 = new_thread->saved_context.r12;
	frame.saved_registers.r13 = new_thread->saved_context.r13;
	frame.saved_registers.r14 = new_thread->saved_context.r14;
	frame.saved_registers.r15 = new_thread->saved_context.r15;
	frame.rip                 = (void*)new_thread->saved_context.rip;
	frame.rflags              = new_thread->saved_context.rflags;
	frame.cs                  = new_thread->saved_context.cs;
	frame.ss                  = new_thread->saved_context.ss;
	frame.saved_registers.interrupt_disable = new_thread->saved_context.interrupt_disable;

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
