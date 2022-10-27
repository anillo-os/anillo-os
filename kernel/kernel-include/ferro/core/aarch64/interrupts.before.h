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
 * AARCH64 implementations of architecture-specific components for interrupts subsystem.
 */

#ifndef _FERRO_CORE_AARCH64_INTERRUPTS_BEFORE_H_
#define _FERRO_CORE_AARCH64_INTERRUPTS_BEFORE_H_

#include <stdint.h>

#include <ferro/base.h>

FERRO_DECLARATIONS_BEGIN;

/**
 * @addtogroup Interrupts
 *
 * @{
 */

/**
 * The type used to represent the interrupt state returned by fint_save() and accepted by fint_restore().
 */
typedef uint64_t fint_state_t;

FERRO_PACKED_STRUCT(fint_frame) {
	uint64_t x0;
	uint64_t x1;
	uint64_t x2;
	uint64_t x3;
	uint64_t x4;
	uint64_t x5;
	uint64_t x6;
	uint64_t x7;
	uint64_t x8;
	uint64_t x9;
	uint64_t x10;
	uint64_t x11;
	uint64_t x12;
	uint64_t x13;
	uint64_t x14;
	uint64_t x15;
	uint64_t x16;
	uint64_t x17;
	uint64_t x18;
	uint64_t x19;
	uint64_t x20;
	uint64_t x21;
	uint64_t x22;
	uint64_t x23;
	uint64_t x24;
	uint64_t x25;
	uint64_t x26;
	uint64_t x27;
	uint64_t x28;
	uint64_t x29; // fp
	uint64_t x30; // lr
	uint64_t elr;
	uint64_t esr;
	uint64_t far;
	uint64_t sp;

	// actually spsr
	uint64_t pstate;

	uint64_t interrupt_disable;
	uint64_t address_space;

	uint64_t fpsr;
	uint64_t fpcr;
	__uint128_t fp_registers[32];
};

// needs to be 16-byte aligned so we can push it onto the stack
FERRO_VERIFY_ALIGNMENT(fint_frame_t, 16);

/**
 * @}
 */

FERRO_DECLARATIONS_END;

#include <ferro/core/interrupts.h>

#endif // _FERRO_CORE_AARCH64_INTERRUPTS_BEFORE_H_
