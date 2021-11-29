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

// arg 1 (rdi) is the handler to invoke (void*)
// arg 2 (rsi) is the saved userspace context (fthread_saved_context*)
.text
.global _farch_syscall_handler_invoke
_farch_syscall_handler_invoke:
	// use r11 as a scratch register to store the handler address
	mov %rdi, %r11

	mov FOFFSET_fthread_saved_context_rdi(%rsi), %rdi
	// load rsi last
	mov FOFFSET_fthread_saved_context_rdx(%rsi), %rdx
	mov FOFFSET_fthread_saved_context_r10(%rsi), %rcx
	mov FOFFSET_fthread_saved_context_r8(%rsi), %r8
	mov FOFFSET_fthread_saved_context_r9(%rsi), %r9

	mov FOFFSET_fthread_saved_context_rsi(%rsi), %rsi

	call *%r11

	// don't touch rax so that we return the handler's return value as our return value

	ret
