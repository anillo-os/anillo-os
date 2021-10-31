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

#define MAKE_IMMEDIATE_HELPER(x) $ ## x
#define MAKE_IMMEDIATE(x) MAKE_IMMEDIATE_HELPER(x)

// arg 1 (rdi) is the new rip
// arg 2 (rsi) is the new rsp
.text
.global farch_uthread_jump_user_frame
farch_uthread_jump_user_frame:
	// no need to load the right interrupt-disable count since userspace can't disable interrupts
	// (and it'll be reset to 1 on syscalls and interrupts)

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
.global farch_uthread_syscall_handler_wrapper
farch_uthread_syscall_handler_wrapper:
	// we're exiting userspace, so do a swapgs for the kernel gs
	swapgs

	// store rflags (from r11) into the temporary spaces in the per-CPU data structure
	mov %r11, %gs:FOFFSET_farch_per_cpu_data_temporary_rflags
	// now we can use r11 as a scratch register

	// load r11 with the uthread data pointer
	mov %gs:FOFFSET_farch_per_cpu_data_current_uthread_data, %r11

	//
	// save the context
	//
	mov %rax, FOFFSET_futhread_data_rax(%r11)
	// rcx is clobbered by syscalls
	mov %rdx, FOFFSET_futhread_data_rdx(%r11)
	mov %rbx, FOFFSET_futhread_data_rbx(%r11)
	mov %rsi, FOFFSET_futhread_data_rsi(%r11)
	mov %rdi, FOFFSET_futhread_data_rdi(%r11)
	mov %rsp, FOFFSET_futhread_data_rsp(%r11)
	mov %rbp, FOFFSET_futhread_data_rbp(%r11)
	mov  %r8,  FOFFSET_futhread_data_r8(%r11)
	mov  %r9,  FOFFSET_futhread_data_r9(%r11)
	mov %r10, FOFFSET_futhread_data_r10(%r11)
	// r11 is clobbered by syscalls
	mov %r12, FOFFSET_futhread_data_r12(%r11)
	mov %r13, FOFFSET_futhread_data_r13(%r11)
	mov %r14, FOFFSET_futhread_data_r14(%r11)
	mov %r15, FOFFSET_futhread_data_r15(%r11)

	// save the rip
	mov %rcx, FOFFSET_futhread_data_rip(%r11)

	// store the temporarily saved rflags
	// (using rcx as a scratch register)
	mov %gs:FOFFSET_farch_per_cpu_data_temporary_rflags, %rcx
	movq %rcx, FOFFSET_futhread_data_rflags(%r11)
	xor %rcx, %rcx

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
	call farch_uthread_syscall_handler

	// okay, we're returning normally from the handler, so we need to head back into userspace

	// at this point, when we're heading back into userspace, any register can serve as our scratch register
	// let's just keep using r11 for consistency

	// load r11 with the uthread data pointer
	mov %gs:FOFFSET_farch_per_cpu_data_current_uthread_data, %r11

	//
	// load the context
	//
	mov FOFFSET_futhread_data_rax(%r11), %rax
	// rcx is clobbered by syscalls
	mov FOFFSET_futhread_data_rdx(%r11), %rdx
	mov FOFFSET_futhread_data_rbx(%r11), %rbx
	mov FOFFSET_futhread_data_rsi(%r11), %rsi
	mov FOFFSET_futhread_data_rdi(%r11), %rdi
	mov FOFFSET_futhread_data_rsp(%r11), %rsp
	mov FOFFSET_futhread_data_rbp(%r11), %rbp
	mov  FOFFSET_futhread_data_r8(%r11),  %r8
	mov  FOFFSET_futhread_data_r9(%r11),  %r9
	mov FOFFSET_futhread_data_r10(%r11), %r10
	// r11 is clobbered by syscalls
	mov FOFFSET_futhread_data_r12(%r11), %r12
	mov FOFFSET_futhread_data_r13(%r11), %r13
	mov FOFFSET_futhread_data_r14(%r11), %r14
	mov FOFFSET_futhread_data_r15(%r11), %r15

	// load the rip into rcx
	mov FOFFSET_futhread_data_rip(%r11), %rcx

	// load the rflags into r11
	mov FOFFSET_futhread_data_rflags(%r11), %r11

	// finally, let's swap out the gs with the user one...
	swapgs

	// ...and head back into userspace
	sysretq
