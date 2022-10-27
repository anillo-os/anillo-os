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

#ifndef _FERRO_ASM_X86_64_HELPERS_HS_
#define _FERRO_ASM_X86_64_HELPERS_HS_

#define MAKE_IMMEDIATE_HELPER(x) $ ## x
#define MAKE_IMMEDIATE(x) MAKE_IMMEDIATE_HELPER(x)

// Pushes a pair of registers onto the stack, keeping it 16-byte aligned.
.macro pushp reg1, reg2
	subq $16, %rsp
	mov \reg1, 0(%rsp)
	mov \reg2, 8(%rsp)
.endm

// Pops a pair of registers off the stack, keeping it 16-byte aligned.
.macro popp reg1, reg2
	mov 0(%rsp), \reg1
	mov 8(%rsp), \reg2
	addq $16, %rsp
.endm

// Pushes a single register onto the stack, keeping it 16-byte aligned.
.macro pushs reg1
	subq $16, %rsp
	mov \reg1, 0(%rsp)
.endm

// Pops a single register off the stack, keeping it 16-byte aligned.
.macro pops reg1
	mov 0(%rsp), \reg1
	addq $16, %rsp
.endm

#endif // _FERRO_ASM_X86_64_HELPERS_HS_
