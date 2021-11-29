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
 * x86_64 scheduler helpers.
 */

#include <gen/ferro/offsets.h>

#define MAKE_IMMEDIATE_HELPER(x) $ ## x
#define MAKE_IMMEDIATE(x) MAKE_IMMEDIATE_HELPER(x)

#define FERRO_GDT_OFFSET_KERNEL_CS $8

// arg 1 (rdi) is a `fthread_saved_context_t*` (which may be `NULL`)
// arg 2 (rsi) is a `fint_frame_t*`
.text
.global _farch_sched_immediate_switch
_farch_sched_immediate_switch:
	//
	// first, save everything (if we have a current thread)
	//

	cmp %rdi, 0
	je 0f

	// start with general-purpose registers
	mov %rax, FOFFSET_fthread_saved_context_rax(%rdi)
	mov %rcx, FOFFSET_fthread_saved_context_rcx(%rdi)
	mov %rdx, FOFFSET_fthread_saved_context_rdx(%rdi)
	mov %rbx, FOFFSET_fthread_saved_context_rbx(%rdi)
	mov %rsi, FOFFSET_fthread_saved_context_rsi(%rdi)
	mov %rdi, FOFFSET_fthread_saved_context_rdi(%rdi)
	mov %rsp, FOFFSET_fthread_saved_context_rsp(%rdi)
	mov %rbp, FOFFSET_fthread_saved_context_rbp(%rdi)
	mov  %r8,  FOFFSET_fthread_saved_context_r8(%rdi)
	mov  %r9,  FOFFSET_fthread_saved_context_r9(%rdi)
	mov %r10, FOFFSET_fthread_saved_context_r10(%rdi)
	mov %r11, FOFFSET_fthread_saved_context_r11(%rdi)
	mov %r12, FOFFSET_fthread_saved_context_r12(%rdi)
	mov %r13, FOFFSET_fthread_saved_context_r13(%rdi)
	mov %r14, FOFFSET_fthread_saved_context_r14(%rdi)
	mov %r15, FOFFSET_fthread_saved_context_r15(%rdi)

	// load the target instruction pointer for re-entry
	lea 1f(%rip), %r10
	mov %r10, FOFFSET_fthread_saved_context_rip(%rdi)

	// save the current rflags
	pushfq
	popq FOFFSET_fthread_saved_context_rflags(%rdi)

	// save the segments
	mov %cs, FOFFSET_fthread_saved_context_cs(%rdi)
	mov %ss, FOFFSET_fthread_saved_context_ss(%rdi)

	// this is where we go if rdi (the current thread context) is `NULL`
	0:

	//
	// let's arm the timer
	//
	pushq %rsi
	call _fsched_arm_timer
	popq %rsi

	//
	// okay, now let's start loading everything
	//

	// load the right interrupt-disable count
	pushq %rsi
	mov FOFFSET_fint_frame_interrupt_disable(%rsi), %rdi
	call _farch_sched_set_interrupt_disable_count
	popq %rsi

	// load general-purpose registers
	mov FOFFSET_fint_frame_rax(%rsi), %rax
	mov FOFFSET_fint_frame_rcx(%rsi), %rcx
	mov FOFFSET_fint_frame_rdx(%rsi), %rdx
	mov FOFFSET_fint_frame_rbx(%rsi), %rbx
	// rsi is loaded later
	mov FOFFSET_fint_frame_rdi(%rsi), %rdi
	// rsp is loaded later (by the iretq)
	mov FOFFSET_fint_frame_rbp(%rsi), %rbp
	mov  FOFFSET_fint_frame_r8(%rsi),  %r8
	mov  FOFFSET_fint_frame_r9(%rsi),  %r9
	// r10 is loaded later
	mov FOFFSET_fint_frame_r11(%rsi), %r11
	mov FOFFSET_fint_frame_r12(%rsi), %r12
	mov FOFFSET_fint_frame_r13(%rsi), %r13
	mov FOFFSET_fint_frame_r14(%rsi), %r14
	mov FOFFSET_fint_frame_r15(%rsi), %r15

	// now for the next part, we setup a fake interrupt return frame

	// first, allocate space for the fake frame
	sub MAKE_IMMEDIATE(FLAYOUT_farch_int_frame_core_SIZE), %rsp

	// now load each of the values
	mov FOFFSET_fint_frame_rip(%rsi), %r10
	mov %r10, FOFFSET_farch_int_frame_core_rip(%rsp)

	mov FOFFSET_fint_frame_cs(%rsi), %r10
	mov %r10, FOFFSET_farch_int_frame_core_cs(%rsp)

	mov FOFFSET_fint_frame_rflags(%rsi), %r10
	mov %r10, FOFFSET_farch_int_frame_core_rflags(%rsp)

	mov FOFFSET_fint_frame_rsp(%rsi), %r10
	mov %r10, FOFFSET_farch_int_frame_core_rsp(%rsp)

	mov FOFFSET_fint_frame_ss(%rsi), %r10
	mov %r10, FOFFSET_farch_int_frame_core_ss(%rsp)

	// load r10 now (doing it earlier would cause it to be overwritten when using it in the fake frame setup)
	mov FOFFSET_fint_frame_r10(%rsi), %r10

	// load rsi now (doing it earlier would cause us to lose the pointer)
	mov FOFFSET_fint_frame_rsi(%rsi), %rsi

	# check if we need to perform a swapgs
	cmpq FERRO_GDT_OFFSET_KERNEL_CS, FOFFSET_farch_int_frame_core_cs(%rsp)
	je 1f
	swapgs
	1:

	// now perform the fake interrupt return
	iretq

	// this is where we come when we are re-entered; we can just return to the caller now
	1:
	ret

// arg 1 (rdi) is the new frame to load (it's a `fint_frame_t*`)
// but it's also rsp, which is more useful since the call to fsched_arm_timer() can overwrite rdi
.text
.global _farch_sched_delayed_switch
_farch_sched_delayed_switch:
	// arm the timer
	call _fsched_arm_timer

	//
	// start loading registers
	//

	// load the right interrupt-disable count
	mov FOFFSET_fint_frame_interrupt_disable(%rsp), %rdi
	call _farch_sched_set_interrupt_disable_count

	// load general-purpose registers
	mov FOFFSET_fint_frame_rax(%rsp), %rax
	mov FOFFSET_fint_frame_rcx(%rsp), %rcx
	mov FOFFSET_fint_frame_rdx(%rsp), %rdx
	mov FOFFSET_fint_frame_rbx(%rsp), %rbx
	mov FOFFSET_fint_frame_rsi(%rsp), %rsi
	mov FOFFSET_fint_frame_rdi(%rsp), %rdi
	// rsp is loaded later (by the iretq)
	mov FOFFSET_fint_frame_rbp(%rsp), %rbp
	mov  FOFFSET_fint_frame_r8(%rsp),  %r8
	mov  FOFFSET_fint_frame_r9(%rsp),  %r9
	mov FOFFSET_fint_frame_r10(%rsp), %r10
	mov FOFFSET_fint_frame_r11(%rsp), %r11
	mov FOFFSET_fint_frame_r12(%rsp), %r12
	mov FOFFSET_fint_frame_r13(%rsp), %r13
	mov FOFFSET_fint_frame_r14(%rsp), %r14
	mov FOFFSET_fint_frame_r15(%rsp), %r15

	// we already have an interrupt frame set up, we just need to move the stack pointer to where we need it
	add MAKE_IMMEDIATE(FOFFSET_fint_frame_rip), %rsp

	# check if we need to perform a swapgs
	cmpq FERRO_GDT_OFFSET_KERNEL_CS, FOFFSET_farch_int_frame_core_cs(%rsp)
	je 1f
	swapgs
	1:

	// now perform the fake interrupt return
	iretq

// similar to farch_sched_delayed_switch(), but does not assume that the frame is already placed on the stack
//
// arg 1 (rdi) is the new frame to load (it's a `fint_frame_t*`)
.text
.global _farch_sched_bootstrap_switch
_farch_sched_bootstrap_switch:
	// arm the timer
	pushq %rdi
	call _fsched_arm_timer
	popq %rdi

	//
	// start loading registers
	//

	// load the right interrupt-disable count
	pushq %rdi
	mov FOFFSET_fint_frame_interrupt_disable(%rdi), %rdi
	call _farch_sched_set_interrupt_disable_count
	popq %rdi

	// load general-purpose registers
	mov FOFFSET_fint_frame_rax(%rdi), %rax
	mov FOFFSET_fint_frame_rcx(%rdi), %rcx
	mov FOFFSET_fint_frame_rdx(%rdi), %rdx
	mov FOFFSET_fint_frame_rbx(%rdi), %rbx
	mov FOFFSET_fint_frame_rsi(%rdi), %rsi
	// rdi is loaded later
	// rsp is loaded later (by the iretq)
	mov FOFFSET_fint_frame_rbp(%rdi), %rbp
	mov  FOFFSET_fint_frame_r8(%rdi),  %r8
	mov  FOFFSET_fint_frame_r9(%rdi),  %r9
	// r10 is loaded later
	mov FOFFSET_fint_frame_r11(%rdi), %r11
	mov FOFFSET_fint_frame_r12(%rdi), %r12
	mov FOFFSET_fint_frame_r13(%rdi), %r13
	mov FOFFSET_fint_frame_r14(%rdi), %r14
	mov FOFFSET_fint_frame_r15(%rdi), %r15

	// now for the next part, we setup a fake interrupt return frame

	// first, allocate space for the fake frame
	sub MAKE_IMMEDIATE(FLAYOUT_farch_int_frame_core_SIZE), %rsp

	// now load each of the values
	mov FOFFSET_fint_frame_rip(%rdi), %r10
	mov %r10, FOFFSET_farch_int_frame_core_rip(%rsp)

	mov FOFFSET_fint_frame_cs(%rdi), %r10
	mov %r10, FOFFSET_farch_int_frame_core_cs(%rsp)

	mov FOFFSET_fint_frame_rflags(%rdi), %r10
	mov %r10, FOFFSET_farch_int_frame_core_rflags(%rsp)

	mov FOFFSET_fint_frame_rsp(%rdi), %r10
	mov %r10, FOFFSET_farch_int_frame_core_rsp(%rsp)

	mov FOFFSET_fint_frame_ss(%rdi), %r10
	mov %r10, FOFFSET_farch_int_frame_core_ss(%rsp)

	// load r10 now (doing it earlier would cause it to be overwritten when using it in the fake frame setup)
	mov FOFFSET_fint_frame_r10(%rdi), %r10

	// load rdi now (doing it earlier would cause us to lose the pointer)
	mov FOFFSET_fint_frame_rdi(%rdi), %rdi

	# check if we need to perform a swapgs
	cmpq FERRO_GDT_OFFSET_KERNEL_CS, FOFFSET_farch_int_frame_core_cs(%rsp)
	je 1f
	swapgs
	1:

	// now perform the fake interrupt return
	iretq
