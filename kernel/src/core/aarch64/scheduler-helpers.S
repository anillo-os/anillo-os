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

/**
 * @file
 *
 * AARCH64 scheduler context-switching helper functions.
 */

#include <gen/ferro/offsets.h>

#define MAKE_IMMEDIATE_HELPER(x, y) x ## y
#define MAKE_IMMEDIATE_HELPER2(x, y) MAKE_IMMEDIATE_HELPER(x, y)
#define MAKE_IMMEDIATE_HASH #
#define MAKE_IMMEDIATE(x) MAKE_IMMEDIATE_HELPER2(MAKE_IMMEDIATE_HASH, x)

// arg 1 (x0) is a `fthread_saved_context_t*` (which may be NULL)
// arg 2 (x1) is a `fthread_saved_context_t*`
.text
.global _farch_sched_immediate_switch
_farch_sched_immediate_switch:
	cbz x0, 0f

	// save general-purpose registers
	stp  x0,  x1, [x0, MAKE_IMMEDIATE( FOFFSET_fthread_saved_context_x0)]
	stp  x2,  x3, [x0, MAKE_IMMEDIATE( FOFFSET_fthread_saved_context_x2)]
	stp  x4,  x5, [x0, MAKE_IMMEDIATE( FOFFSET_fthread_saved_context_x4)]
	stp  x6,  x7, [x0, MAKE_IMMEDIATE( FOFFSET_fthread_saved_context_x6)]
	stp  x8,  x9, [x0, MAKE_IMMEDIATE( FOFFSET_fthread_saved_context_x8)]
	stp x10, x11, [x0, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_x10)]
	stp x12, x13, [x0, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_x12)]
	stp x14, x15, [x0, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_x14)]
	stp x16, x17, [x0, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_x16)]
	stp x18, x19, [x0, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_x18)]
	stp x20, x21, [x0, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_x20)]
	stp x22, x23, [x0, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_x22)]
	stp x24, x25, [x0, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_x24)]
	stp x26, x27, [x0, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_x26)]
	stp x28, x29, [x0, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_x28)]

	// load the return address into x19 and store x30 and this address into the appropriate locations
	ldr x19, =1f
	stp x30, x19, [x0, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_x30)]

	// store the stack pointer
	// this must be SP_EL0 because immediate switches cannot be performed in an interrupt context
	mov x19, sp
	str x19, [x0, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_sp)]

	// pstate and interrupt_disable have already been saved by the C code that calls us

	// this is where we go if x0 (the current thread context) is `NULL`
	0:

	// on AARCH64, we can now perform the exact same steps as a delayed switch, so just branch to it
	mov x0, x1
	b _farch_sched_delayed_switch

	// this is where we come when we are re-entered; we can just return to the caller now
	1:
	ret

// arg 1 (x0) is a `fthread_saved_context_t*`
.text
.global _farch_sched_delayed_switch
_farch_sched_delayed_switch:
	// move the context pointer from x0 to x19
	mov x19, x0

	// arm the timer
	bl _fsched_arm_timer

	// load the right interrupt-disable count
	ldr x0, [x19, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_interrupt_disable)]
	bl _farch_sched_set_interrupt_disable_count

	// load the return address into the ELR
	ldr x0, [x19, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_pc)]
	msr elr_el1, x0

	// load the stack pointer
	// we MUST be using SP_EL0 here (and so must our target code after the switch)
	ldr x0, [x19, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_sp)]
	mov sp, x0

	// load the pstate into the SPSR
	ldr x0, [x19, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_pstate)]
	msr spsr_el1, x0

	// load general-purpose registers
	ldp  x0,  x1, [x19, MAKE_IMMEDIATE( FOFFSET_fthread_saved_context_x0)]
	ldp  x2,  x3, [x19, MAKE_IMMEDIATE( FOFFSET_fthread_saved_context_x2)]
	ldp  x4,  x5, [x19, MAKE_IMMEDIATE( FOFFSET_fthread_saved_context_x4)]
	ldp  x6,  x7, [x19, MAKE_IMMEDIATE( FOFFSET_fthread_saved_context_x6)]
	ldp  x8,  x9, [x19, MAKE_IMMEDIATE( FOFFSET_fthread_saved_context_x8)]
	ldp x10, x11, [x19, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_x10)]
	ldp x12, x13, [x19, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_x12)]
	ldp x14, x15, [x19, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_x14)]
	ldp x16, x17, [x19, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_x16)]
	//ldp x18, x19, [x19, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_x18)] // load x19 later, to avoid overwriting the pointer
	ldp x20, x21, [x19, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_x20)]
	ldp x22, x23, [x19, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_x22)]
	ldp x24, x25, [x19, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_x24)]
	ldp x26, x27, [x19, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_x26)]
	ldp x28, x29, [x19, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_x28)]

	// finally, load x18 and x19
	ldp x18, x19, [x19, MAKE_IMMEDIATE(FOFFSET_fthread_saved_context_x18)]

	// and perform the switch
	eret

// arg 1 (x0) is a `fthread_saved_context_t*`
.text
.global _farch_sched_bootstrap_switch
_farch_sched_bootstrap_switch:
	// we can actually just perform a delayed switch.
	// on AARCH64, a bootstrap switch and a delayed switch are exactly the same.
	b _farch_sched_delayed_switch
