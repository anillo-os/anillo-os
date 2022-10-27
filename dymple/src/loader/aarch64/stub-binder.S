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

.text

//
// layout of sp on entry:
//   [0..8]   = lazy binding info offset (from the compressed dynamic linker information structure)
//   [8..16]  = image handle address (typically the address of `_dyld_private` in the image)
//
.align 4
.global _dymple_bind_stub     // defined in C
.global _dymple_bind_stub_raw // defined here
_dymple_bind_stub_raw:
	// save the frame pointer and link register and update the frame pointer
	sub sp, sp, #16
	stp fp, lr, [sp]
	mov fp, sp

	// save parameters
	sub sp, sp, #80
	stp x0,  x1, [sp, #0]
	stp x2,  x3, [sp, #16]
	stp x4,  x5, [sp, #32]
	stp x6,  x7, [sp, #48]
	stp x8, xzr, [sp, #64]

	//
	// now the layout of the stack looks like:
	//   [0..8]     = x0
	//   [8..16]    = x1
	//   [16..24]   = x2
	//   [24..32]   = x3
	//   [32..40]   = x4
	//   [40..48]   = x5
	//   [48..56]   = x6
	//   [56..64]   = x7
	//   [64..72]   = x8
	//   [72..80]   = padding
	//   [80..88]   = saved frame pointer
	//   [88..96]   = saved link register
	//   [96..104]  = lazy binding info offset
	//   [104..112] = image handle address
	//

	// here's the problem, though:
	// our C code expects the info structure to have the image handle and *then* the lazy binding info.
	// so we need to swap those two now that we have some registers available.
	ldp x0, x1, [sp, #96]
	stp x1, x0, [sp, #96]

	// now load a pointer to the info structure into arg1 (x0)
	add x0, sp, #96

	// call our actual binder
	bl _dymple_bind_stub

	// our binder returns the address where we should jump.
	// load it into x9
	mov x9, x0

	// now restore parameters
	ldp x0,  x1, [sp, #0]
	ldp x2,  x3, [sp, #16]
	ldp x4,  x5, [sp, #32]
	ldp x6,  x7, [sp, #48]
	ldp x8, xzr, [sp, #64]

	// restore the stack to the saved frame
	mov sp, fp

	// load the frame pointer and link register
	ldp fp, lr, [sp]

	// now remove the FP and LR from the stack, but also remove the lazy binding info offset and image handle from the stack
	add sp, sp, #32

	// now jump to the target function
	br x9

.align 4
.global ___stack_chk_fail
___stack_chk_fail:
	udf #0

.align 4
.data
.global ___stack_chk_guard
___stack_chk_guard:
	.long 0
