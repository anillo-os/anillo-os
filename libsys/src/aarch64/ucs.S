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

#include <ferro/asm/aarch64/helpers.hS>
#include <libsys/gen/offsets.h>

.align 4
.text

// arg 0 (x0) is a pointer to the context to save into
.global ___sys_ucs_save
___sys_ucs_save:
	// we don't want to re-run the function if we load the context,
	// so save the address of the function exit point
	adr x1, 0f
	stp x1, xzr, [x0, MAKE_IMMEDIATE(FOFFSET_sys_ucs_context_ip)]

	// now save a bunch of registers
	stp x19, x20, [x0, MAKE_IMMEDIATE(FOFFSET_sys_ucs_context_x19)]
	stp x21, x22, [x0, MAKE_IMMEDIATE(FOFFSET_sys_ucs_context_x21)]
	stp x23, x24, [x0, MAKE_IMMEDIATE(FOFFSET_sys_ucs_context_x23)]
	stp x25, x26, [x0, MAKE_IMMEDIATE(FOFFSET_sys_ucs_context_x25)]
	stp x27, x28, [x0, MAKE_IMMEDIATE(FOFFSET_sys_ucs_context_x27)]
	stp x29, x30, [x0, MAKE_IMMEDIATE(FOFFSET_sys_ucs_context_x29)]

	// now store the sp
	mov x1, sp
	str x1, [x0, MAKE_IMMEDIATE(FOFFSET_sys_ucs_context_sp)]

	// now save the fpcr
	mrs x1, fpcr
	str x1, [x0, MAKE_IMMEDIATE(FOFFSET_sys_ucs_context_fpcr)]

	// now save a bunch of floating-point registers
	stp  d8,  d9, [x0, MAKE_IMMEDIATE(FOFFSET_sys_ucs_context_fp_registers) + (8 * 0)]
	stp d10, d11, [x0, MAKE_IMMEDIATE(FOFFSET_sys_ucs_context_fp_registers) + (8 * 2)]
	stp d12, d13, [x0, MAKE_IMMEDIATE(FOFFSET_sys_ucs_context_fp_registers) + (8 * 4)]
	stp d14, d15, [x0, MAKE_IMMEDIATE(FOFFSET_sys_ucs_context_fp_registers) + (8 * 6)]

0:
	ret

// arg 0 (x0) is an optional pointer to the context to save into
// arg 1 (x1) is a pointer to the context to load
.global ___sys_ucs_switch
___sys_ucs_switch:
	// check if arg 0 is null
	// if it is, skip the context saving portion of this function
	cbz x0, 0f

	//
	// save the old context
	//

	// first, save the address to return to if the context is loaded
	adr x2, 1f
	stp x2, xzr, [x0, MAKE_IMMEDIATE(FOFFSET_sys_ucs_context_ip)]

	// now save a bunch of registers
	stp x19, x20, [x0, MAKE_IMMEDIATE(FOFFSET_sys_ucs_context_x19)]
	stp x21, x22, [x0, MAKE_IMMEDIATE(FOFFSET_sys_ucs_context_x21)]
	stp x23, x24, [x0, MAKE_IMMEDIATE(FOFFSET_sys_ucs_context_x23)]
	stp x25, x26, [x0, MAKE_IMMEDIATE(FOFFSET_sys_ucs_context_x25)]
	stp x27, x28, [x0, MAKE_IMMEDIATE(FOFFSET_sys_ucs_context_x27)]
	stp x29, x30, [x0, MAKE_IMMEDIATE(FOFFSET_sys_ucs_context_x29)]

	// now store the sp
	mov x2, sp
	str x2, [x0, MAKE_IMMEDIATE(FOFFSET_sys_ucs_context_sp)]

	// now save the fpcr
	mrs x2, fpcr
	str x2, [x0, MAKE_IMMEDIATE(FOFFSET_sys_ucs_context_fpcr)]

	// now save a bunch of floating-point registers
	stp  d8,  d9, [x0, MAKE_IMMEDIATE(FOFFSET_sys_ucs_context_fp_registers) + (8 * 0)]
	stp d10, d11, [x0, MAKE_IMMEDIATE(FOFFSET_sys_ucs_context_fp_registers) + (8 * 2)]
	stp d12, d13, [x0, MAKE_IMMEDIATE(FOFFSET_sys_ucs_context_fp_registers) + (8 * 4)]
	stp d14, d15, [x0, MAKE_IMMEDIATE(FOFFSET_sys_ucs_context_fp_registers) + (8 * 6)]

0:
	//
	// load the new context
	//

	// load the new instruction pointer to x3 and the context into x0
	ldp x3, x0, [x1, MAKE_IMMEDIATE(FOFFSET_sys_ucs_context_ip)]

	// load a bunch of registers
	ldp x19, x20, [x1, MAKE_IMMEDIATE(FOFFSET_sys_ucs_context_x19)]
	ldp x21, x22, [x1, MAKE_IMMEDIATE(FOFFSET_sys_ucs_context_x21)]
	ldp x23, x24, [x1, MAKE_IMMEDIATE(FOFFSET_sys_ucs_context_x23)]
	ldp x25, x26, [x1, MAKE_IMMEDIATE(FOFFSET_sys_ucs_context_x25)]
	ldp x27, x28, [x1, MAKE_IMMEDIATE(FOFFSET_sys_ucs_context_x27)]
	ldp x29, x30, [x1, MAKE_IMMEDIATE(FOFFSET_sys_ucs_context_x29)]

	// now load the sp
	ldr x2, [x1, MAKE_IMMEDIATE(FOFFSET_sys_ucs_context_sp)]
	mov sp, x2

	// now load the fpcr
	ldr x2, [x1, MAKE_IMMEDIATE(FOFFSET_sys_ucs_context_fpcr)]
	msr fpcr, x2

	// now load a bunch of floating-point registers
	ldp  d8,  d9, [x1, MAKE_IMMEDIATE(FOFFSET_sys_ucs_context_fp_registers) + (8 * 0)]
	ldp d10, d11, [x1, MAKE_IMMEDIATE(FOFFSET_sys_ucs_context_fp_registers) + (8 * 2)]
	ldp d12, d13, [x1, MAKE_IMMEDIATE(FOFFSET_sys_ucs_context_fp_registers) + (8 * 4)]
	ldp d14, d15, [x1, MAKE_IMMEDIATE(FOFFSET_sys_ucs_context_fp_registers) + (8 * 6)]

	// finally, jump to the new instruction pointer
	br x3

1:
	// we only ever get here if someone loads the context we saved
	ret
