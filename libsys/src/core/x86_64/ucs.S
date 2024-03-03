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

#include <ferro/asm/x86_64/helpers.hS>
#include <libsys/gen/offsets.h>
#include <ferro/asm/common.hS>

// NOTE: these functions do not care about stack alignment,
//       so they CANNOT call C code. well, they also can't call
//       C code because they mess with the stack.

// arg 0 (rdi) is a pointer to the context to save into
.text
.global FERRO_SYM(__sys_ucs_save)
FERRO_SYM(__sys_ucs_save):
	// we don't want to re-run the function if we load the context,
	// so save the address of the function exit point
	lea 0f(%rip), %rsi
	mov %rsi, FOFFSET_sys_ucs_context_rip(%rdi)

	// now save a bunch of registers
	mov %rbx, FOFFSET_sys_ucs_context_rbx(%rdi)
	mov %rsp, FOFFSET_sys_ucs_context_rsp(%rdi)
	mov %rbp, FOFFSET_sys_ucs_context_rbp(%rdi)
	mov %r12, FOFFSET_sys_ucs_context_r12(%rdi)
	mov %r13, FOFFSET_sys_ucs_context_r13(%rdi)
	mov %r14, FOFFSET_sys_ucs_context_r14(%rdi)
	mov %r15, FOFFSET_sys_ucs_context_r15(%rdi)

	// now save the floating point control registers
	stmxcsr FOFFSET_sys_ucs_context_mxcsr(%rdi)
	fnstcw FOFFSET_sys_ucs_context_x87_cw(%rdi)

0:
	ret

// arg 0 (rdi) is an optional pointer to the context to save into
// arg 1 (rsi) is a pointer to the context to load
.global FERRO_SYM(__sys_ucs_switch)
FERRO_SYM(__sys_ucs_switch):
	// check if arg 0 is non-null
	test %rdi, %rdi
	jz 0f

	//
	// arg 0 is non-null, so save the current context
	//

	// first, save the address to return to if the context is loaded
	lea 1f(%rip), %rdx
	mov %rdx, FOFFSET_sys_ucs_context_rip(%rdi)

	// now save a bunch of registers
	mov %rbx, FOFFSET_sys_ucs_context_rbx(%rdi)
	mov %rsp, FOFFSET_sys_ucs_context_rsp(%rdi)
	mov %rbp, FOFFSET_sys_ucs_context_rbp(%rdi)
	mov %r12, FOFFSET_sys_ucs_context_r12(%rdi)
	mov %r13, FOFFSET_sys_ucs_context_r13(%rdi)
	mov %r14, FOFFSET_sys_ucs_context_r14(%rdi)
	mov %r15, FOFFSET_sys_ucs_context_r15(%rdi)

	// now save the floating point control registers
	stmxcsr FOFFSET_sys_ucs_context_mxcsr(%rdi)
	fnstcw FOFFSET_sys_ucs_context_x87_cw(%rdi)

0:
	//
	// load the new context
	//

	// load the new instruction pointer to rax; we'll jump to it later
	mov FOFFSET_sys_ucs_context_rip(%rsi), %rax

	// load the context argument
	mov FOFFSET_sys_ucs_context_rdi(%rsi), %rdi

	// load a bunch of registers
	mov FOFFSET_sys_ucs_context_rbx(%rsi), %rbx
	mov FOFFSET_sys_ucs_context_rsp(%rsi), %rsp
	mov FOFFSET_sys_ucs_context_rbp(%rsi), %rbp
	mov FOFFSET_sys_ucs_context_r12(%rsi), %r12
	mov FOFFSET_sys_ucs_context_r13(%rsi), %r13
	mov FOFFSET_sys_ucs_context_r14(%rsi), %r14
	mov FOFFSET_sys_ucs_context_r15(%rsi), %r15

	// load the floating point control registers
	ldmxcsr FOFFSET_sys_ucs_context_mxcsr(%rsi)
	fldcw FOFFSET_sys_ucs_context_x87_cw(%rsi)

	// finally, jump to the new instruction pointer
	jmp *%rax

1:
	// we only ever get here if someone loads the context we saved
	ret
