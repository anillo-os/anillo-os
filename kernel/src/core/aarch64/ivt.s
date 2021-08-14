#
# This file is part of Anillo OS
# Copyright (C) 2021 Anillo OS Developers
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU Lesser General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.
#

#
# src/core/aarch64/ivt.s
#
# AARCH64 IVT (interrupt vector table), mainly for forwarding to C handlers
#

#
# The interrupt vector table (IVT) is defined in this assembly file rather than in a C file because of the following reasons:
#   1. Because the IVT entires are actually instructions, each of which is limited to 32 instructions, and we will certainly
#      want to use more, we need to branch somewhere else. However, branches are PC-relative and limited to 128MiB, so
#      we want to keep the table and the instructions it branches to close to each other.
#   2. Some other reason that I forgot as I was writing this :/
#

.macro ivt_block prefix
	.irp type, sync, irq, fiq, serror
		.balign 0x80
			b \prefix\()_\type
	.endr
.endm

.macro loop_handlers prefix
	.irp type, sync, irq, fiq, serror
		.text
		\prefix\()_\type\():
			b .
	.endr
.endm

# most of the handler stub code is based on https://krinkinmu.github.io/2021/01/10/aarch64-interrupt-handling.html
.macro handler_stub prefix, type, c_func_name
	.text
	\prefix\()_\type\():
		# allocate the stack space we need
		sub sp, sp, #192

		# save necessary registers and a few extra ones
		# (x29 doesn't need to be saved, but it's useful)
		stp  x0,  x1, [sp,   #0]
		stp  x2,  x3, [sp,  #16]
		stp  x4,  x5, [sp,  #32]
		stp  x6,  x7, [sp,  #48]
		stp  x8,  x9, [sp,  #64]
		stp x10, x11, [sp,  #80]
		stp x12, x13, [sp,  #96]
		stp x14, x15, [sp, #112]
		stp x16, x17, [sp, #128]
		stp x18, x29, [sp, #144]

		# save the ELR so it can be easily read and modified as needed by the handler
		# also save the LR because it's modified by the branch-and-link instruction later
		mrs x0, ELR_EL1
		stp x30, x0, [sp, #160]

		# save important exception registers
		mrs x0, ESR_EL1
		mrs x1, FAR_EL1
		stp x0, x1, [sp, #176]

		# move a pointer to the structure (the stack pointer) into the first argument register
		# and then call the function
		mov x0, sp
		bl \c_func_name

		# the handler might've modified the ELR, so assign it
		# also restore the saved LR
		ldp x30, x0, [sp, #160]
		msr ELR_EL1, x0

		# restore the saved registers
		ldp  x0,  x1, [sp,   #0]
		ldp  x2,  x3, [sp,  #16]
		ldp  x4,  x5, [sp,  #32]
		ldp  x6,  x7, [sp,  #48]
		ldp  x8,  x9, [sp,  #64]
		ldp x10, x11, [sp,  #80]
		ldp x12, x13, [sp,  #96]
		ldp x14, x15, [sp, #112]
		ldp x16, x17, [sp, #128]
		ldp x18, x29, [sp, #144]

		# restore the stack pointer
		add sp, sp, #192

		# return from the exception
		eret
.endm

# create stubs for the C handlers for these
.irp prefix, current_with_sp0, current_with_spx
	.irp type, sync, irq, fiq, serror
		handler_stub \prefix, \type, fint_handler_\prefix\()_\type
	.endr
.endr

# these handler types are currently unused, so just loop forever if they're triggered
.irp prefix, lower_with_aarch64, lower_with_aarch32
	loop_handlers \prefix
.endr

.text
.global fint_ivt
.balign 0x800
fint_ivt:
	ivt_block current_with_sp0
	ivt_block current_with_spx
	ivt_block lower_with_aarch64
	ivt_block lower_with_aarch32
