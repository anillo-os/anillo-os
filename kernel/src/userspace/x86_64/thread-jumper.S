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

#include <gen/ferro/offsets.h>
#include <ferro/asm/x86_64/helpers.hS>

// arg 1 (rdi) is the new rip
// arg 2 (rsi) is the new rsp
.text
.global _farch_uthread_jump_user_frame
_farch_uthread_jump_user_frame:
	pushq %rbp
	mov %rsp, %rbp

	// load the right interrupt-disable count (0 for userspace)
	pushp %rdi, %rsi
	// zero-out rsi
	xor %edi, %edi
	call _farch_uthread_set_interrupt_disable_count
	popp %rdi, %rsi

	// create a fake XSAVE area that will clear out the state
	subq %gs:FOFFSET_farch_per_cpu_data_xsave_area_size, %rsp
	andq $-64, %rsp
	mov %rsp, %r12

	pushp %rdi, %rsi
	mov %r12, %rdi
	xor %esi, %esi
	mov %gs:FOFFSET_farch_per_cpu_data_xsave_area_size, %rdx
	call _simple_memset
	popp %rdi, %rsi

	// initialize MXCSR (along with the mask)
	movq $0x1f80, 24(%r12)
	movq $0xffbf, 28(%r12)

	// load the XSAVE feature mask
	movq %gs:FOFFSET_farch_per_cpu_data_xsave_features, %rdx
	mov %edx, %eax
	shr $32, %rdx

	// now perform a fake XRSTOR
	xrstor64 (%r12)

	// zero-out general-purpose registers
	// (note that here we use a 32-bit register clear trick for maximum efficiency)
	xor  %eax,  %eax
	// rcx is loaded later
	xor  %edx,  %edx
	xor  %ebx,  %ebx
	// rsi is loaded later
	// rdi is loaded later
	// rsp is loaded later
	xor  %ebp,  %ebp
	xor  %r8d,  %r8d
	xor  %r9d,  %r9d
	xor %r10d, %r10d
	// r11 is loaded later
	xor %r12d, %r12d
	xor %r13d, %r13d
	xor %r14d, %r14d
	xor %r15d, %r15d

	// now load the new rip into rcx and the new rsp (into rsp)
	mov %rdi, %rcx
	mov %rsi, %rsp

	// and clear out rdi and rsi
	xor %edi, %edi
	xor %esi, %esi

	// now load the new rflags value into r11
	mov $0x202, %r11

	// we're entering userspace, so do a swapgs for the user gs
	swapgs

	// and finally, do the sysret
	sysretq

.text
.global _farch_uthread_syscall_handler_wrapper
_farch_uthread_syscall_handler_wrapper:
	// we're exiting userspace, so do a swapgs for the kernel gs
	swapgs

	// store rflags (from r11) into the temporary spaces in the per-CPU data structure
	mov %r11, %gs:FOFFSET_farch_per_cpu_data_temporary_rflags
	// now we can use r11 as a scratch register

	// load r11 with the uthread data pointer
	mov %gs:FOFFSET_farch_per_cpu_data_current_uthread_data, %r11
	// now load it with the register context
	mov FOFFSET_futhread_data_saved_syscall_context(%r11), %r11

	//
	// save the context
	//
	mov %rax, FOFFSET_fthread_saved_context_rax(%r11)
	// rcx is clobbered by syscalls
	mov %rdx, FOFFSET_fthread_saved_context_rdx(%r11)
	mov %rbx, FOFFSET_fthread_saved_context_rbx(%r11)
	mov %rsi, FOFFSET_fthread_saved_context_rsi(%r11)
	mov %rdi, FOFFSET_fthread_saved_context_rdi(%r11)
	mov %rsp, FOFFSET_fthread_saved_context_rsp(%r11)
	mov %rbp, FOFFSET_fthread_saved_context_rbp(%r11)
	mov  %r8,  FOFFSET_fthread_saved_context_r8(%r11)
	mov  %r9,  FOFFSET_fthread_saved_context_r9(%r11)
	mov %r10, FOFFSET_fthread_saved_context_r10(%r11)
	// r11 is clobbered by syscalls
	mov %r12, FOFFSET_fthread_saved_context_r12(%r11)
	mov %r13, FOFFSET_fthread_saved_context_r13(%r11)
	mov %r14, FOFFSET_fthread_saved_context_r14(%r11)
	mov %r15, FOFFSET_fthread_saved_context_r15(%r11)

	// save the rip
	mov %rcx, FOFFSET_fthread_saved_context_rip(%r11)

	// store the temporarily saved rflags
	// (using rcx as a scratch register)
	mov %gs:FOFFSET_farch_per_cpu_data_temporary_rflags, %rcx
	movq %rcx, FOFFSET_fthread_saved_context_rflags(%r11)
	xor %rcx, %rcx

	//
	// save the XSAVE state
	//

	// load the XSAVE area address into a temporary register
	lea FOFFSET_fthread_saved_context_xsave_area(%r11), %r12

	// load the XSAVE feature mask
	movq %gs:FOFFSET_farch_per_cpu_data_xsave_features, %rdx
	mov %edx, %eax
	shr $32, %rdx

	// now perform XSAVE
	xsave64 (%r12)

	// zero-out general-purpose registers
	// (note that here we use a 32-bit register clear trick for maximum efficiency)
	xor  %eax,  %eax
	xor  %ecx,  %ecx
	xor  %edx,  %edx
	xor  %ebx,  %ebx
	xor  %esi,  %esi
	xor  %edi,  %edi
	// rsp is loaded later
	xor  %ebp,  %ebp
	xor  %r8d,  %r8d
	xor  %r9d,  %r9d
	xor %r10d, %r10d
	// r11 is already clobbered
	xor %r12d, %r12d
	xor %r13d, %r13d
	xor %r14d, %r14d
	xor %r15d, %r15d

	// load the address of the current thread's structure
	mov %gs:FOFFSET_farch_per_cpu_data_current_thread, %r11

	// load the thread's kernel stack base into rsp
	mov FOFFSET_fthread_private_stack_base(%r11), %rsp
	// and move it to the right place (the end) by adding its size
	add FOFFSET_fthread_private_stack_size(%r11), %rsp

	// zero out r11, just for fun
	xor %r11d, %r11d

	// call our C handler
	call _farch_uthread_syscall_handler

	// okay, we're returning normally from the handler, so we need to head back into userspace

	// load the right interrupt-disable count (0 for userspace)
	// zero-out rsi
	xor %edi, %edi
	call _farch_uthread_set_interrupt_disable_count

	// at this point, when we're heading back into userspace, any register can serve as our scratch register
	// let's just keep using r11 for consistency

	// load r11 with the uthread data pointer
	mov %gs:FOFFSET_farch_per_cpu_data_current_uthread_data, %r11
	// now load it with the register context
	mov FOFFSET_futhread_data_saved_syscall_context(%r11), %r11

	//
	// restore the XSAVE state
	//

	// load the XSAVE area address into a temporary register
	lea FOFFSET_fthread_saved_context_xsave_area(%r11), %r12

	// load the XSAVE feature mask
	movq %gs:FOFFSET_farch_per_cpu_data_xsave_features, %rdx
	mov %edx, %eax
	shr $32, %rdx

	// now perform XRSTOR
	xrstor64 (%r12)

	//
	// load the context
	//
	mov FOFFSET_fthread_saved_context_rax(%r11), %rax
	// rcx is clobbered by syscalls
	mov FOFFSET_fthread_saved_context_rdx(%r11), %rdx
	mov FOFFSET_fthread_saved_context_rbx(%r11), %rbx
	mov FOFFSET_fthread_saved_context_rsi(%r11), %rsi
	mov FOFFSET_fthread_saved_context_rdi(%r11), %rdi
	mov FOFFSET_fthread_saved_context_rsp(%r11), %rsp
	mov FOFFSET_fthread_saved_context_rbp(%r11), %rbp
	mov  FOFFSET_fthread_saved_context_r8(%r11),  %r8
	mov  FOFFSET_fthread_saved_context_r9(%r11),  %r9
	mov FOFFSET_fthread_saved_context_r10(%r11), %r10
	// r11 is clobbered by syscalls
	mov FOFFSET_fthread_saved_context_r12(%r11), %r12
	mov FOFFSET_fthread_saved_context_r13(%r11), %r13
	mov FOFFSET_fthread_saved_context_r14(%r11), %r14
	mov FOFFSET_fthread_saved_context_r15(%r11), %r15

	// load the rip into rcx
	mov FOFFSET_fthread_saved_context_rip(%r11), %rcx

	// load the rflags into r11
	mov FOFFSET_fthread_saved_context_rflags(%r11), %r11

	// finally, let's swap out the gs with the user one...
	swapgs

	// ...and head back into userspace
	sysretq
