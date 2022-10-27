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

.text
.global _libsyscall_invoke
_libsyscall_invoke:
	pushq %rbp
	mov %rsp, %rbp

	// load the syscall number into rax
	mov %rdi, %rax

	// load all the arguments where they belong
	mov %rsi, %rdi
	mov %rdx, %rsi
	mov %rcx, %rdx
	mov  %r8, %r10 // rcx is replaced with r10
	mov  %r9,  %r8
	mov 16(%rsp), %r9 // the last argument is on the stack

	// perform the syscall
	syscall

	// the return value is already in rax
	popq %rbp
	ret
