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

// TODO: we also need to xsave here so dymple can use floating point registers.
//       in the current state, if we try to call memset and friends (which are optimized with SSE),
//       we may corrupt the function call.

//
// layout of rsp on entry:
//   [0..8]   = image handle address (typically the address of `_dyld_private` in the image)
//   [8..16]  = lazy binding info offset (from the compressed dynamic linker information structure)
//   [16..24] = return address
//
.text
.globl _dymple_bind_stub     // defined in C
.globl _dymple_bind_stub_raw // defined here
_dymple_bind_stub_raw:
	// save rbp (for debugging purposes)
	// (this also aligns the stack back to 16 bytes)
	pushq %rbp
	mov %rsp, %rbp

	// make room on the stack for the caller's parameters
	subq $0x40, %rsp

	// now save the parameters
	movq %rdi, 0x00(%rsp)
	movq %rsi, 0x08(%rsp)
	movq %rdx, 0x10(%rsp)
	movq %rcx, 0x18(%rsp)
	movq  %r8, 0x20(%rsp)
	movq  %r9, 0x28(%rsp)
	movq %rbx, 0x30(%rsp)
	movq %rax, 0x38(%rsp)

	//
	// now the layout of rsp looks like:
	//   [0..8]   = rdi
	//   [8..16]  = rsi
	//   [16..24] = rdx
	//   [24..32] = rcx
	//   [32..40] = r8
	//   [40..48] = r9
	//   [48..56] = rbx
	//   [56..64] = rax
	//   [64..72] = saved rbp
	//   [72..80] = image handle address
	//   [80..88] = lazy binding info offset
	//   [88..96] = return address
	//
	// rbx and rax are sort of weird to save since they're not really parameters,
	// but Apple's dyld saves them, so we do, too.
	//

	// determine the size of the xsave area
	// TODO: cache this value somehow
	mov $0x0d, %eax
	xor %ecx, %ecx
	cpuid

	// set arg 1 (rdi) to a pointer to the info structure
	lea 0x48(%rsp), %rdi

	// the xsave area size is in ecx;
	// let's make room for it now
	// also temporarily save the stack pointer to restore to in r11
	mov %rsp, %r11
	subq %rcx, %rsp

	// align the stack for the xsave area
	andq $-64, %rsp

	// clear out the XSAVE area header
	movq $0, 512(%rsp)
	movq $0, 520(%rsp)
	movq $0, 528(%rsp)
	movq $0, 536(%rsp)
	movq $0, 544(%rsp)
	movq $0, 552(%rsp)
	movq $0, 560(%rsp)
	movq $0, 568(%rsp)

	// perform the xsave
	xsave64 (%rsp)

	// make space for the saved stack pointer and feature flags
	subq $16, %rsp

	// save the stack pointer to restore after the xrstor
	movq %r11, 0x00(%rsp)

	// save the feature flags
	movl %eax, 0x08(%rsp)
	movl %edx, 0x0c(%rsp)

	// call our actual binder
	call _dymple_bind_stub

	// our binder returns the address where we should jump.
	// load it into r11 (which is guaranteed not to be used for parameters).
	movq %rax, %r11

	// load the xsave feature flags
	movl 0x08(%rsp), %eax
	movl 0x0c(%rsp), %edx

	// temporarily save the stack pointer to restore to into r8
	movq 0x00(%rsp), %r8

	// remove the saved stack pointer and feature flags
	addq $16, %rsp

	// perform the xrstor
	xrstor64 (%rsp)

	// restore the stack pointer
	mov %r8, %rsp

	// let's restore our saved parameters
	movq 0x00(%rsp), %rdi
	movq 0x08(%rsp), %rsi
	movq 0x10(%rsp), %rdx
	movq 0x18(%rsp), %rcx
	movq 0x20(%rsp),  %r8
	movq 0x28(%rsp),  %r9
	movq 0x30(%rsp), %rbx
	movq 0x38(%rsp), %rax

	// restore the stack back to the old rbp
	movq %rbp, %rsp

	// restore the old rbp
	popq %rbp

	// and remove our lazy binding info
	addq $0x10, %rsp

	// now the only thing on the stack should be the return address,
	// which is for the function we're about to jump to
	jmp *%r11

.global ___stack_chk_fail
___stack_chk_fail:
	ud2

.data
.global ___stack_chk_guard
___stack_chk_guard:
	.long 0
