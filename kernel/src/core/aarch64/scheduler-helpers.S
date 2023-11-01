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
 * AARCH64 scheduler context-switching helper functions.
 */

#include <gen/ferro/offsets.h>
#include <ferro/asm/common.hS>

// arg 1 (x0) is a `fthread_saved_context_t*` (which may be NULL)
// arg 2 (x1) is a `fthread_saved_context_t*`
.align 4
.text
.global FERRO_SYM(farch_sched_immediate_switch)
FERRO_SYM(farch_sched_immediate_switch):
	cbz x0, 0f

	// save general-purpose registers
	stp  x0,  x1, [x0, MAKE_IMMEDIATE( FOFFSET_fthread_saved_context_x0)]
	stp  x2,  x3, [x0, MAKE_IMMEDIATE( FOFFSET_fthread_saved_context_x2)]
	stp  x4,  x5, [x0, MAKE_IMMEDIATE( FOFFSET_fthread_saved_context_x4)]
	stp  x6,  x7, [x0, MAKE_IMMEDIATE( FOFFSET_fthread_saved_context_x6)]
	stp  x8,  x9, [x0, MAKE_IMMEDIATE( FOFFSET_fthread_saved_context_x8)]
	stp x10, x11, [x0, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_x10)]
	stp x12, x13, [x0, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_x12)]
	stp x14, x15, [x0, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_x14)]
	stp x16, x17, [x0, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_x16)]
	stp x18, x19, [x0, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_x18)]
	stp x20, x21, [x0, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_x20)]
	stp x22, x23, [x0, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_x22)]
	stp x24, x25, [x0, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_x24)]
	stp x26, x27, [x0, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_x26)]
	stp x28, x29, [x0, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_x28)]

	// load the return address into x19 and store x30 and this address into the appropriate locations
	ldr x19, =1f
	stp x30, x19, [x0, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_x30)]

	// store the stack pointer
	// this must be SP_EL0 because immediate switches cannot be performed in an interrupt context
	mov x19, sp
	str x19, [x0, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_sp)]

	// pstate and interrupt_disable have already been saved by the C code that calls us

	// now save the floating point control and status
	mrs x19, fpsr
	mrs x20, fpcr
	stp x19, x20, [x0, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_fpsr)]

	// and finally, save all the floating point registers
	stp  q0,  q1, [x0, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_fp_registers) + (16 *  0)]
	stp  q2,  q3, [x0, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_fp_registers) + (16 *  2)]
	stp  q4,  q5, [x0, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_fp_registers) + (16 *  4)]
	stp  q6,  q7, [x0, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_fp_registers) + (16 *  6)]
	stp  q8,  q9, [x0, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_fp_registers) + (16 *  8)]
	stp q10, q11, [x0, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_fp_registers) + (16 * 10)]
	stp q12, q13, [x0, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_fp_registers) + (16 * 12)]
	stp q14, q15, [x0, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_fp_registers) + (16 * 14)]
	stp q16, q17, [x0, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_fp_registers) + (16 * 16)]
	stp q18, q19, [x0, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_fp_registers) + (16 * 18)]
	stp q20, q21, [x0, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_fp_registers) + (16 * 20)]
	stp q22, q23, [x0, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_fp_registers) + (16 * 22)]
	stp q24, q25, [x0, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_fp_registers) + (16 * 24)]
	stp q26, q27, [x0, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_fp_registers) + (16 * 26)]
	stp q28, q29, [x0, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_fp_registers) + (16 * 28)]
	stp q30, q31, [x0, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_fp_registers) + (16 * 30)]

	// this is where we go if x0 (the current thread context) is `NULL`
	0:

	// on AARCH64, we can now perform the exact same steps as a delayed switch, so just branch to it
	mov x0, x1
	b FERRO_SYM(farch_sched_delayed_switch)

	// this is where we come when we are re-entered; we can just return to the caller now
	1:
	ret

// arg 1 (x0) is a `fthread_saved_context_t*`
.align 4
.text
.global FERRO_SYM(farch_sched_delayed_switch)
FERRO_SYM(farch_sched_delayed_switch):
	// move the context pointer from x0 to x19
	mov x19, x0

	// arm the timer
	bl FERRO_SYM(fsched_arm_timer)

	// load the right interrupt-disable count
	ldr x0, [x19, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_interrupt_disable)]
	mrs x1, tpidr_el1
	str x0, [x1, MAKE_IMMEDIATE(FOFFSET_farch_per_cpu_data_outstanding_interrupt_disable_count)]

	// load the return address (first into x30 and then into the ELR) and also load the link register
	ldp x30, x0, [x19, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_x30)]
	msr elr_el1, x0

	// load the floating point control and status
	ldp x0, x1, [sp, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_fpsr)]
	msr fpsr, x0
	msr fpcr, x1

	// now load the floating point registers
	ldp  q0,  q1, [sp, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_fp_registers) + (16 *  0)]
	ldp  q2,  q3, [sp, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_fp_registers) + (16 *  2)]
	ldp  q4,  q5, [sp, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_fp_registers) + (16 *  4)]
	ldp  q6,  q7, [sp, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_fp_registers) + (16 *  6)]
	ldp  q8,  q9, [sp, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_fp_registers) + (16 *  8)]
	ldp q10, q11, [sp, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_fp_registers) + (16 * 10)]
	ldp q12, q13, [sp, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_fp_registers) + (16 * 12)]
	ldp q14, q15, [sp, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_fp_registers) + (16 * 14)]
	ldp q16, q17, [sp, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_fp_registers) + (16 * 16)]
	ldp q18, q19, [sp, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_fp_registers) + (16 * 18)]
	ldp q20, q21, [sp, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_fp_registers) + (16 * 20)]
	ldp q22, q23, [sp, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_fp_registers) + (16 * 22)]
	ldp q24, q25, [sp, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_fp_registers) + (16 * 24)]
	ldp q26, q27, [sp, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_fp_registers) + (16 * 26)]
	ldp q28, q29, [sp, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_fp_registers) + (16 * 28)]
	ldp q30, q31, [sp, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_fp_registers) + (16 * 30)]

	// load the stack pointer (first into x0 then into sp) and load the pstate (first into x1 and then into the SPSR)
	// we MUST be using SP_EL0 here (and so must our target code after the switch)
	ldp x0, x1, [x19, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_sp)]
	mov sp, x0
	msr spsr_el1, x1

	// load general-purpose registers
	ldp  x0,  x1, [x19, MAKE_IMMEDIATE( FOFFSET_fthread_saved_context_x0)]
	ldp  x2,  x3, [x19, MAKE_IMMEDIATE( FOFFSET_fthread_saved_context_x2)]
	ldp  x4,  x5, [x19, MAKE_IMMEDIATE( FOFFSET_fthread_saved_context_x4)]
	ldp  x6,  x7, [x19, MAKE_IMMEDIATE( FOFFSET_fthread_saved_context_x6)]
	ldp  x8,  x9, [x19, MAKE_IMMEDIATE( FOFFSET_fthread_saved_context_x8)]
	ldp x10, x11, [x19, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_x10)]
	ldp x12, x13, [x19, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_x12)]
	ldp x14, x15, [x19, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_x14)]
	ldp x16, x17, [x19, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_x16)]
	//ldp x18, x19, [x19, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_x18)] // load x19 later, to avoid overwriting the pointer
	ldp x20, x21, [x19, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_x20)]
	ldp x22, x23, [x19, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_x22)]
	ldp x24, x25, [x19, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_x24)]
	ldp x26, x27, [x19, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_x26)]
	ldp x28, x29, [x19, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_x28)]

	// finally, load x18 and x19
	ldp x18, x19, [x19, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_x18)]

	// and perform the switch
	eret

// arg 1 (x0) is a `fthread_saved_context_t*`
.align 4
.text
.global FERRO_SYM(farch_sched_bootstrap_switch)
FERRO_SYM(farch_sched_bootstrap_switch):
	// we can actually just perform a delayed switch.
	// on AARCH64, a bootstrap switch and a delayed switch are exactly the same.
	b FERRO_SYM(farch_sched_delayed_switch)
