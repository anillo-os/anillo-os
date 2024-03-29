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
 * x86_64 interrupt wrappers which forward to C handlers.
 */

#include <gen/ferro/offsets.h>
#include <ferro/asm/x86_64/helpers.hS>
#include <ferro/asm/common.hS>

#define FERRO_GDT_OFFSET_KERNEL_CS $8

.altmacro

.macro wrapper func, has_code
	.text
	.global FERRO_SYM(farch_int_wrapper_\()\func)
	FERRO_SYM(farch_int_wrapper_\()\func\()):
		# check if we need to perform a swapgs
		.if \has_code == 0
			cmpq FERRO_GDT_OFFSET_KERNEL_CS, FOFFSET_farch_int_frame_core_cs(%rsp)
		.else
			cmpq FERRO_GDT_OFFSET_KERNEL_CS, FOFFSET_farch_int_frame_with_code_cs(%rsp)
		.endif
		je 1f
		swapgs
		1:

		.if \has_code == 0
			subq MAKE_IMMEDIATE(FOFFSET_fint_frame_core), %rsp
		.else
			subq MAKE_IMMEDIATE(FOFFSET_fint_frame_code), %rsp
		.endif

		mov %rax, FOFFSET_fint_frame_rax(%rsp)
		mov %rcx, FOFFSET_fint_frame_rcx(%rsp)
		mov %rdx, FOFFSET_fint_frame_rdx(%rsp)
		mov %rbx, FOFFSET_fint_frame_rbx(%rsp)
		mov %rsi, FOFFSET_fint_frame_rsi(%rsp)
		mov %rdi, FOFFSET_fint_frame_rdi(%rsp)
		mov %rbp, FOFFSET_fint_frame_rbp(%rsp)
		mov  %r8,  FOFFSET_fint_frame_r8(%rsp)
		mov  %r9,  FOFFSET_fint_frame_r9(%rsp)
		mov %r10, FOFFSET_fint_frame_r10(%rsp)
		mov %r11, FOFFSET_fint_frame_r11(%rsp)
		mov %r12, FOFFSET_fint_frame_r12(%rsp)
		mov %r13, FOFFSET_fint_frame_r13(%rsp)
		mov %r14, FOFFSET_fint_frame_r14(%rsp)
		mov %r15, FOFFSET_fint_frame_r15(%rsp)
		// leave interrupt-disable alone; higher-level wrappers will take care of it
		movw %ds, FOFFSET_fint_frame_ds(%rsp)
		movw %es, FOFFSET_fint_frame_es(%rsp)
		movw %fs, FOFFSET_fint_frame_fs(%rsp)
		movw %gs, FOFFSET_fint_frame_gs(%rsp)

		.if \has_code == 0
			movq $0, FOFFSET_fint_frame_code(%rsp)
		.endif

		mov %rsp, %rdi

		// now let's perform an XSAVE

		// first, allocate space for the XSAVE area
		subq %gs:FOFFSET_farch_per_cpu_data_xsave_area_size, %rsp

		// now, align it to 64 bytes
		andq $-64, %rsp

		// now set this pointer in the frame
		mov %rsp, FOFFSET_fint_frame_xsave_area(%rdi)

		// load the XSAVE feature mask
		movq %gs:FOFFSET_farch_per_cpu_data_xsave_features, %rdx
		mov %edx, %eax
		shr $32, %rdx

		// clear out the XSAVE area header
		movq $0, 512(%rsp)
		movq $0, 520(%rsp)
		movq $0, 528(%rsp)
		movq $0, 536(%rsp)
		movq $0, 544(%rsp)
		movq $0, 552(%rsp)
		movq $0, 560(%rsp)
		movq $0, 568(%rsp)

		// now perform XSAVE
		xsave64 (%rsp)

		pushs <%rdi>
		call FERRO_SYM(farch_int_\()\func\()_handler)
		pops <%rdi>

		// now let's perform an XRSTOR

		// load the XSAVE feature mask
		movq %gs:FOFFSET_farch_per_cpu_data_xsave_features, %rdx
		mov %edx, %eax
		shr $32, %rdx

		// now perform XRSTOR
		xrstor64 (%rsp)

		// move the stack pointer to point to the start of the frame
		mov %rdi, %rsp

		mov FOFFSET_fint_frame_rax(%rsp), %rax
		mov FOFFSET_fint_frame_rcx(%rsp), %rcx
		mov FOFFSET_fint_frame_rdx(%rsp), %rdx
		mov FOFFSET_fint_frame_rbx(%rsp), %rbx
		mov FOFFSET_fint_frame_rsi(%rsp), %rsi
		mov FOFFSET_fint_frame_rdi(%rsp), %rdi
		mov FOFFSET_fint_frame_rbp(%rsp), %rbp
		mov  FOFFSET_fint_frame_r8(%rsp),  %r8
		mov  FOFFSET_fint_frame_r9(%rsp),  %r9
		mov FOFFSET_fint_frame_r10(%rsp), %r10
		mov FOFFSET_fint_frame_r11(%rsp), %r11
		mov FOFFSET_fint_frame_r12(%rsp), %r12
		mov FOFFSET_fint_frame_r13(%rsp), %r13
		mov FOFFSET_fint_frame_r14(%rsp), %r14
		mov FOFFSET_fint_frame_r15(%rsp), %r15
		movw FOFFSET_fint_frame_ds(%rsp),  %ds
		movw FOFFSET_fint_frame_es(%rsp),  %es
		//movw FOFFSET_fint_frame_fs(%rsp),  %fs
		//movw FOFFSET_fint_frame_gs(%rsp),  %gs

		add MAKE_IMMEDIATE(FOFFSET_fint_frame_core), %rsp

		# check if we need to perform a swapgs
		cmpq FERRO_GDT_OFFSET_KERNEL_CS, FOFFSET_farch_int_frame_core_cs(%rsp)
		je 1f
		swapgs
		1:

		iretq
.endm

//
// processor-reserved interrupts
//

wrapper division_error, 0
wrapper debug, 0
wrapper breakpoint, 0
wrapper double_fault, 1
wrapper general_protection, 1
wrapper page_fault, 1
wrapper invalid_opcode, 0
wrapper simd_exception, 0

//
// user-defined interrupts
//

.macro misc_wrapper number
	wrapper interrupt_\()\number, 0
.endm

.set i, 0
.rept 224
	misc_wrapper %i
	.set i, i + 1
.endr
