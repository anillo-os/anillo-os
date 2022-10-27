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

#include <gen/ferro/offsets.h>
#include <ferro/asm/aarch64/helpers.hS>

// arg 1 (x0) is `void* handler`
// arg 2 (x1) is `fthread_saved_context_t* user_context`
.align 4
.text
.global _farch_syscall_handler_invoke
_farch_syscall_handler_invoke:
	// save the handler address to x9
	mov x9, x0
	// and the saved context pointer to x10
	mov x10, x1

	// load arguments
	ldp x0, x1, [x10, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_x0)]
	ldp x2, x3, [x10, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_x2)]
	ldp x4, x5, [x10, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_x4)]
	ldp x6, x7, [x10, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_x6)]

	// branch to the handler (without linking)
	br x9
