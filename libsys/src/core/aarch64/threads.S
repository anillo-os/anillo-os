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
#include <ferro/asm/aarch64/helpers.hS>
#include <ferro/asm/common.hS>

.align 4
.text
.global FERRO_SYM(__sys_thread_entry)
FERRO_SYM(__sys_thread_entry):
	// the initial stack layout is the same as on x86_64:
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

	// make room on the stack to preserve our arguments
	// (and add some alignment space)
	sub sp, sp, #32

	// set up the thread (e.g. TLS)
	ldr x0, [sp, #8]
	bl FERRO_SYM(__sys_thread_setup)

	// now load the arguments for the entry function (as well as the function pointer itself)
	// x1 = thread object
	// x0 = context argument
	// x11 = entry function
	ldr x1, [sp, #8]
	ldp x0, x11, [sp, #16]

	// remove our arguments from the stack
	// (and give the entry function the full stack space)
	add sp, sp, #32

	// now call the entry function
	blr x11

	// this should not return
	bl FERRO_SYM(__sys_thread_exit_self)
	udf #0

.align 4
.global FERRO_SYM(__sys_thread_die)
FERRO_SYM(__sys_thread_die):
	// x0 = thread id
	// x1 = stack pointer to free (maybe null)

	// save the thread id to x19 and the (possibly null) stack pointer to free into x20
	mov x19, x0
	mov x20, x1

	// check if we have anything to free
	// if not (i.e. if it's null/zero), go ahead and die
	cbz x20, 0f

	// otherwise, let's free the stack first

	// syscall number in x8
	mov x8, MAKE_IMMEDIATE(FERRO_SYSCALL_page_free)
	// set argument 0 to the address to free
	mov x0, x20
	// make the syscall
	svc #0

	// now die
0:
	// syscall number in x8
	mov x8, MAKE_IMMEDIATE(FERRO_SYSCALL_thread_kill)
	// set argument 0 to the thread id to kill
	mov x0, x19
	// make the syscall
	svc #0

	// should never get here
	udf #0
