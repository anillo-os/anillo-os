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
 * AARCH64 IVT (interrupt vector table), mainly for forwarding to C handlers.
 */

#include <gen/ferro/offsets.h>
#include <ferro/asm/common.hS>

#define MAKE_IMMEDIATE_HELPER(x, y) x ## y
#define MAKE_IMMEDIATE_HELPER2(x, y) MAKE_IMMEDIATE_HELPER(x, y)
#define MAKE_IMMEDIATE_HASH #
#define MAKE_IMMEDIATE(x) MAKE_IMMEDIATE_HELPER2(MAKE_IMMEDIATE_HASH, x)

//
// The interrupt vector table (IVT) is defined in this assembly file rather than in a C file because of the following reasons:
//   1. Because the IVT entires are actually instructions, each of which is limited to 32 instructions, and we will certainly
//      want to use more, we need to branch somewhere else. However, branches are PC-relative and limited to 128MiB, so
//      we want to keep the table and the instructions it branches to close to each other.
//   2. Some other reason that I forgot as I was writing this :/
//

.macro ivt_block prefix
	.irp type, sync, irq, fiq, serror
		.balign 0x80
			b FERRO_SYM(\()\prefix\()_\type)
	.endr
.endm

.macro loop_handlers prefix
	.irp type, sync, irq, fiq, serror
		.align 4
		.text
		FERRO_SYM(\()\prefix\()_\type\()):
			b .
	.endr
.endm

// most of the handler stub code is based on https://krinkinmu.github.io/2021/01/10/aarch64-interrupt-handling.html
.macro handler_stub prefix, type, c_func_name
	.align 4
	.text
	FERRO_SYM(\()\prefix\()_\type\()):
		// we should already be using the exception stack right here

		// allocate the stack space we need
		sub sp, sp, MAKE_IMMEDIATE(FLAYOUT_fint_frame_SIZE)

		//
		// start saving registers
		//

		// save all registers (to make it easier to context-switch in an interrupt)
		stp  x0,  x1, [sp, MAKE_IMMEDIATE( FOFFSET_fint_frame_x0)]
		stp  x2,  x3, [sp, MAKE_IMMEDIATE( FOFFSET_fint_frame_x2)]
		stp  x4,  x5, [sp, MAKE_IMMEDIATE( FOFFSET_fint_frame_x4)]
		stp  x6,  x7, [sp, MAKE_IMMEDIATE( FOFFSET_fint_frame_x6)]
		stp  x8,  x9, [sp, MAKE_IMMEDIATE( FOFFSET_fint_frame_x8)]
		stp x10, x11, [sp, MAKE_IMMEDIATE(FOFFSET_fint_frame_x10)]
		stp x12, x13, [sp, MAKE_IMMEDIATE(FOFFSET_fint_frame_x12)]
		stp x14, x15, [sp, MAKE_IMMEDIATE(FOFFSET_fint_frame_x14)]
		stp x16, x17, [sp, MAKE_IMMEDIATE(FOFFSET_fint_frame_x16)]
		stp x18, x19, [sp, MAKE_IMMEDIATE(FOFFSET_fint_frame_x18)]
		stp x20, x21, [sp, MAKE_IMMEDIATE(FOFFSET_fint_frame_x20)]
		stp x22, x23, [sp, MAKE_IMMEDIATE(FOFFSET_fint_frame_x22)]
		stp x24, x25, [sp, MAKE_IMMEDIATE(FOFFSET_fint_frame_x24)]
		stp x26, x27, [sp, MAKE_IMMEDIATE(FOFFSET_fint_frame_x26)]
		stp x28, x29, [sp, MAKE_IMMEDIATE(FOFFSET_fint_frame_x28)]

		// save x30 (the link register) and the ELR
		mrs x0, elr_el1
		stp x30, x0, [sp, MAKE_IMMEDIATE(FOFFSET_fint_frame_x30)]

		// save important exception registers
		mrs x0, esr_el1
		mrs x1, far_el1
		stp x0, x1, [sp, MAKE_IMMEDIATE(FOFFSET_fint_frame_esr)]

		// save the stack pointer and processor state
		mrs x0, sp_el0
		mrs x1, spsr_el1
		stp x0, x1, [sp, MAKE_IMMEDIATE(FOFFSET_fint_frame_sp)]

		// now save the floating point control and status
		mrs x0, fpsr
		mrs x1, fpcr
		stp x0, x1, [sp, MAKE_IMMEDIATE(FOFFSET_fint_frame_fpsr)]

		// and finally, save all the floating point registers
		stp  q0,  q1, [sp, MAKE_IMMEDIATE(FOFFSET_fint_frame_fp_registers) + (16 *  0)]
		stp  q2,  q3, [sp, MAKE_IMMEDIATE(FOFFSET_fint_frame_fp_registers) + (16 *  2)]
		stp  q4,  q5, [sp, MAKE_IMMEDIATE(FOFFSET_fint_frame_fp_registers) + (16 *  4)]
		stp  q6,  q7, [sp, MAKE_IMMEDIATE(FOFFSET_fint_frame_fp_registers) + (16 *  6)]
		stp  q8,  q9, [sp, MAKE_IMMEDIATE(FOFFSET_fint_frame_fp_registers) + (16 *  8)]
		stp q10, q11, [sp, MAKE_IMMEDIATE(FOFFSET_fint_frame_fp_registers) + (16 * 10)]
		stp q12, q13, [sp, MAKE_IMMEDIATE(FOFFSET_fint_frame_fp_registers) + (16 * 12)]
		stp q14, q15, [sp, MAKE_IMMEDIATE(FOFFSET_fint_frame_fp_registers) + (16 * 14)]
		stp q16, q17, [sp, MAKE_IMMEDIATE(FOFFSET_fint_frame_fp_registers) + (16 * 16)]
		stp q18, q19, [sp, MAKE_IMMEDIATE(FOFFSET_fint_frame_fp_registers) + (16 * 18)]
		stp q20, q21, [sp, MAKE_IMMEDIATE(FOFFSET_fint_frame_fp_registers) + (16 * 20)]
		stp q22, q23, [sp, MAKE_IMMEDIATE(FOFFSET_fint_frame_fp_registers) + (16 * 22)]
		stp q24, q25, [sp, MAKE_IMMEDIATE(FOFFSET_fint_frame_fp_registers) + (16 * 24)]
		stp q26, q27, [sp, MAKE_IMMEDIATE(FOFFSET_fint_frame_fp_registers) + (16 * 26)]
		stp q28, q29, [sp, MAKE_IMMEDIATE(FOFFSET_fint_frame_fp_registers) + (16 * 28)]
		stp q30, q31, [sp, MAKE_IMMEDIATE(FOFFSET_fint_frame_fp_registers) + (16 * 30)]

		// move a pointer to the structure (the stack pointer) into the first argument register
		// and then call the function
		mov x0, sp
		bl FERRO_SYM(\()\c_func_name)

		//
		// great, now let's load stuff
		//

		// load the floating point control and status
		ldp x0, x1, [sp, MAKE_IMMEDIATE(FOFFSET_fint_frame_fpsr)]
		msr fpsr, x0
		msr fpcr, x1

		// now load the floating point registers
		ldp  q0,  q1, [sp, MAKE_IMMEDIATE(FOFFSET_fint_frame_fp_registers) + (16 *  0)]
		ldp  q2,  q3, [sp, MAKE_IMMEDIATE(FOFFSET_fint_frame_fp_registers) + (16 *  2)]
		ldp  q4,  q5, [sp, MAKE_IMMEDIATE(FOFFSET_fint_frame_fp_registers) + (16 *  4)]
		ldp  q6,  q7, [sp, MAKE_IMMEDIATE(FOFFSET_fint_frame_fp_registers) + (16 *  6)]
		ldp  q8,  q9, [sp, MAKE_IMMEDIATE(FOFFSET_fint_frame_fp_registers) + (16 *  8)]
		ldp q10, q11, [sp, MAKE_IMMEDIATE(FOFFSET_fint_frame_fp_registers) + (16 * 10)]
		ldp q12, q13, [sp, MAKE_IMMEDIATE(FOFFSET_fint_frame_fp_registers) + (16 * 12)]
		ldp q14, q15, [sp, MAKE_IMMEDIATE(FOFFSET_fint_frame_fp_registers) + (16 * 14)]
		ldp q16, q17, [sp, MAKE_IMMEDIATE(FOFFSET_fint_frame_fp_registers) + (16 * 16)]
		ldp q18, q19, [sp, MAKE_IMMEDIATE(FOFFSET_fint_frame_fp_registers) + (16 * 18)]
		ldp q20, q21, [sp, MAKE_IMMEDIATE(FOFFSET_fint_frame_fp_registers) + (16 * 20)]
		ldp q22, q23, [sp, MAKE_IMMEDIATE(FOFFSET_fint_frame_fp_registers) + (16 * 22)]
		ldp q24, q25, [sp, MAKE_IMMEDIATE(FOFFSET_fint_frame_fp_registers) + (16 * 24)]
		ldp q26, q27, [sp, MAKE_IMMEDIATE(FOFFSET_fint_frame_fp_registers) + (16 * 26)]
		ldp q28, q29, [sp, MAKE_IMMEDIATE(FOFFSET_fint_frame_fp_registers) + (16 * 28)]
		ldp q30, q31, [sp, MAKE_IMMEDIATE(FOFFSET_fint_frame_fp_registers) + (16 * 30)]

		// load the stack pointer and processor state
		ldp x0, x1, [sp, MAKE_IMMEDIATE(FOFFSET_fint_frame_sp)]
		msr sp_el0, x0
		msr spsr_el1, x1

		// load x30 (the link register) and the ELR
		ldp x30, x0, [sp, MAKE_IMMEDIATE(FOFFSET_fint_frame_x30)]
		msr elr_el1, x0

		// load the saved general-purpose registers
		ldp  x0,  x1, [sp, MAKE_IMMEDIATE( FOFFSET_fint_frame_x0)]
		ldp  x2,  x3, [sp, MAKE_IMMEDIATE( FOFFSET_fint_frame_x2)]
		ldp  x4,  x5, [sp, MAKE_IMMEDIATE( FOFFSET_fint_frame_x4)]
		ldp  x6,  x7, [sp, MAKE_IMMEDIATE( FOFFSET_fint_frame_x6)]
		ldp  x8,  x9, [sp, MAKE_IMMEDIATE( FOFFSET_fint_frame_x8)]
		ldp x10, x11, [sp, MAKE_IMMEDIATE(FOFFSET_fint_frame_x10)]
		ldp x12, x13, [sp, MAKE_IMMEDIATE(FOFFSET_fint_frame_x12)]
		ldp x14, x15, [sp, MAKE_IMMEDIATE(FOFFSET_fint_frame_x14)]
		ldp x16, x17, [sp, MAKE_IMMEDIATE(FOFFSET_fint_frame_x16)]
		ldp x18, x19, [sp, MAKE_IMMEDIATE(FOFFSET_fint_frame_x18)]
		ldp x20, x21, [sp, MAKE_IMMEDIATE(FOFFSET_fint_frame_x20)]
		ldp x22, x23, [sp, MAKE_IMMEDIATE(FOFFSET_fint_frame_x22)]
		ldp x24, x25, [sp, MAKE_IMMEDIATE(FOFFSET_fint_frame_x24)]
		ldp x26, x27, [sp, MAKE_IMMEDIATE(FOFFSET_fint_frame_x26)]
		ldp x28, x29, [sp, MAKE_IMMEDIATE(FOFFSET_fint_frame_x28)]

		// restore the stack pointer
		add sp, sp, MAKE_IMMEDIATE(FLAYOUT_fint_frame_SIZE)

		// return from the exception
		eret
.endm

// create stubs for the C handlers for these
.irp prefix, current_with_sp0, current_with_spx, lower_with_aarch64
	.irp type, sync, irq, fiq, serror
		handler_stub \prefix, \type, fint_handler_\prefix\()_\type
	.endr
.endr

// these handler types are currently unused, so just loop forever if they're triggered
.irp prefix, lower_with_aarch32
	loop_handlers \prefix
.endr

.align 4
.text
.global FERRO_SYM(fint_ivt)
.balign 0x800
FERRO_SYM(fint_ivt):
	ivt_block current_with_sp0
	ivt_block current_with_spx
	ivt_block lower_with_aarch64
	ivt_block lower_with_aarch32
