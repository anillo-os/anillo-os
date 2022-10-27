/*
 * This file is part of Anillo OS
 * Copyright (C) 2022 Anillo OS Developers
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

#include <gen/ferro/offsets.h>
#include <ferro/asm/aarch64/helpers.hS>

// arg 1 (x0) is `void* pc`
// arg 2 (x1) is `void* sp`
.align 4
.text
.global _farch_uthread_jump_user_frame
_farch_uthread_jump_user_frame:
	// load the PC into the ELR
	msr elr_el1, x0

	// load the SP
	// (note that we assume that we're currently using SP_EL0)
	mov sp, x1

	// load the new PSTATE into the SPSR
	//
	// we can just use `xzr` because:
	//  * the exception level is zero (for EL0)
	//  * the stack selector is zero (for SP_EL0)
	//  * interrupts are unmasked (so none of the interrupt masking bits are set)
	//  * none of the condition flags are set
	//  * software step is disabled (so SS is not set)
	//  * the execution state is valid (so IL is not set)
	msr spsr_el1, xzr

	// load the right interrupt-disable count (0 for userspace)
	mrs x9, tpidr_el1
	str xzr, [x9, MAKE_IMMEDIATE(FOFFSET_farch_per_cpu_data_outstanding_interrupt_disable_count)]

	// zero-out general-purpose registers
	mov x0, xzr
	mov x1, xzr
	mov x2, xzr
	mov x3, xzr
	mov x4, xzr
	mov x5, xzr
	mov x6, xzr
	mov x7, xzr
	mov x8, xzr
	mov x9, xzr
	mov x10, xzr
	mov x11, xzr
	mov x12, xzr
	mov x13, xzr
	mov x14, xzr
	mov x15, xzr
	mov x16, xzr
	mov x17, xzr
	mov x18, xzr
	mov x19, xzr
	mov x20, xzr
	mov x21, xzr
	mov x22, xzr
	mov x23, xzr
	mov x24, xzr
	mov x25, xzr
	mov x26, xzr
	mov x27, xzr
	mov x28, xzr
	mov x29, xzr
	mov x30, xzr

	// initialize fpsr and fpcr
	msr fpsr, xzr
	msr fpcr, xzr

	// zero-out floating point registers
	movi d0, #0
	movi d1, #0
	movi d2, #0
	movi d3, #0
	movi d4, #0
	movi d5, #0
	movi d6, #0
	movi d7, #0
	movi d8, #0
	movi d9, #0
	movi d10, #0
	movi d11, #0
	movi d12, #0
	movi d13, #0
	movi d14, #0
	movi d15, #0
	movi d16, #0
	movi d17, #0
	movi d18, #0
	movi d19, #0
	movi d20, #0
	movi d21, #0
	movi d22, #0
	movi d23, #0
	movi d24, #0
	movi d25, #0
	movi d26, #0
	movi d27, #0
	movi d28, #0
	movi d29, #0
	movi d30, #0
	movi d31, #0

	// now perform the fake exception return
	eret

// arg 1 (x0) is `fthread_saved_context_t* saved_syscall_context`
.align 4
.text
.global _farch_uthread_return_to_userspace
_farch_uthread_return_to_userspace:
	// load the PC into the ELR and load X30 (the link register)
	ldp x30, x1, [x0, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_x30)]
	msr elr_el1, x1

	// load the floating point control and status
	ldp x1, x2, [x0, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_fpsr)]
	msr fpsr, x1
	msr fpcr, x2

	// now load the floating point registers
	ldp  q0,  q1, [x0, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_fp_registers) + (16 *  0)]
	ldp  q2,  q3, [x0, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_fp_registers) + (16 *  2)]
	ldp  q4,  q5, [x0, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_fp_registers) + (16 *  4)]
	ldp  q6,  q7, [x0, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_fp_registers) + (16 *  6)]
	ldp  q8,  q9, [x0, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_fp_registers) + (16 *  8)]
	ldp q10, q11, [x0, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_fp_registers) + (16 * 10)]
	ldp q12, q13, [x0, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_fp_registers) + (16 * 12)]
	ldp q14, q15, [x0, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_fp_registers) + (16 * 14)]
	ldp q16, q17, [x0, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_fp_registers) + (16 * 16)]
	ldp q18, q19, [x0, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_fp_registers) + (16 * 18)]
	ldp q20, q21, [x0, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_fp_registers) + (16 * 20)]
	ldp q22, q23, [x0, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_fp_registers) + (16 * 22)]
	ldp q24, q25, [x0, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_fp_registers) + (16 * 24)]
	ldp q26, q27, [x0, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_fp_registers) + (16 * 26)]
	ldp q28, q29, [x0, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_fp_registers) + (16 * 28)]
	ldp q30, q31, [x0, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_fp_registers) + (16 * 30)]

	// load the SP and load the PSTATE into the SPSR
	ldp x1, x2, [x0, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_sp)]
	mov sp, x1
	msr spsr_el1, x2

	// load the right interrupt-disable count (0 for userspace)
	ldr x1, [x0, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_interrupt_disable)]
	mrs x2, tpidr_el1
	str x1, [x2, MAKE_IMMEDIATE(FOFFSET_farch_per_cpu_data_outstanding_interrupt_disable_count)]

	// now load general-purpose registers
	//
	// load x0 and x1 last; x0 points to the context we're loading
	//ldp  x0,  x1, [x0, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_x0)]
	ldp  x2,  x3, [x0, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_x2)]
	ldp  x4,  x5, [x0, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_x4)]
	ldp  x6,  x7, [x0, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_x6)]
	ldp  x8,  x9, [x0, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_x8)]
	ldp x10, x11, [x0, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_x10)]
	ldp x12, x13, [x0, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_x12)]
	ldp x14, x15, [x0, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_x14)]
	ldp x16, x17, [x0, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_x16)]
	ldp x18, x19, [x0, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_x18)]
	ldp x20, x21, [x0, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_x20)]
	ldp x22, x23, [x0, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_x22)]
	ldp x24, x25, [x0, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_x24)]
	ldp x26, x27, [x0, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_x26)]
	ldp x28, x29, [x0, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_x28)]

	// now we can load x0 and x1
	ldp x0, x1, [x0, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_x0)]

	// and perform a fake exception return
	eret
