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

#include <gen/ferro/userspace/syscall-index.h>
#include <ferro/asm/x86_64/helpers.hS>

.text
.global ___sys_thread_entry
___sys_thread_entry:
	// this is the stack layout upon entry:
	// -------------------- < rsp (stack top)
	// |  entry function  | < rsp - 8
	// | ---------------- |
	// |   context  arg   | < rsp - 16
	// | ---------------- |
	// |  thread  object  | < rsp - 24
	// --------------------
	//
	// after we make space for them (with required alignment), the stack layout is this:
	// -------------------- < stack top (rsp + 32)
	// |  entry function  | < rsp + 24
	// | ---------------- |
	// |   context  arg   | < rsp + 16
	// | ---------------- |
	// |  thread  object  | < rsp + 8
	// | ---------------- |
	// | req'd  alignment | < rsp + 0
	// -------------------- < rsp
	//
	// all registers (except rsp) should be 0

	// subtract enough space from rsp to preserve our arguments (and maintain 16-byte alignment)
	sub $32, %rsp

	// set up the thread (e.g. TLS)
	movq 8(%rsp), %rdi
	call ___sys_thread_setup

	// now load the arguments for the entry function (as well as the function pointer itself)
	movq 8(%rsp), %rsi
	movq 16(%rsp), %rdi
	movq 24(%rsp), %rax

	// move the stack pointer back to the very top;
	// we don't need the arguments any more, so let's allow the entry function to use the full stack space
	add $32, %rsp

	// now call the entry function
	call *%rax

	// this should not return
	call ___sys_thread_exit_self
	ud2

.global ___sys_thread_die
___sys_thread_die:
	// rdi = thread id
	// rsi = stack pointer to free (maybe null)

	// save the thread id to r12 and the stack pointer to free to r13
	mov %rdi, %r12
	mov %rsi, %r13

	// check if we have anything to free
	test %r13, %r13
	// if not, go ahead and die
	jz 0f

	// syscall number in rax
	mov MAKE_IMMEDIATE(FERRO_SYSCALL_page_free), %rax
	// set argument 0 to the address to free
	mov %r13, %rdi
	// make the syscall
	syscall

	// now die
0:
	// syscall number in rax
	mov MAKE_IMMEDIATE(FERRO_SYSCALL_thread_kill), %rax
	// set argument 0 to the thread id to kill
	mov %r12, %rdi
	// make the syscall
	syscall

	// should never get here
	ud2
