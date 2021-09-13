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
 * x86_64 implementations of architecture-specific components for interrupts subsystem.
 */

#ifndef _FERRO_CORE_X86_64_INTERRUPTS_BEFORE_H_
#define _FERRO_CORE_X86_64_INTERRUPTS_BEFORE_H_

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

FERRO_PACKED_STRUCT(farch_int_saved_registers) {
	uint64_t rax;
	uint64_t rcx;
	uint64_t rdx;
	uint64_t rbx;
	uint64_t rsi;
	uint64_t rdi;
	// no RSP; this is saved by the CPU
	uint64_t rbp;
	uint64_t r8;
	uint64_t r9;
	uint64_t r10;
	uint64_t r11;
	uint64_t r12;
	uint64_t r13;
	uint64_t r14;
	uint64_t r15;

	// not actually a register, but is per-CPU and should be saved and restored
	uint64_t interrupt_disable;

	// here's a bit of packing; these 4 `uint16_t`s fit nicely here
	uint16_t ds;
	uint16_t es;
	uint16_t fs;
	uint16_t gs;
};

FERRO_PACKED_STRUCT(farch_int_frame_core) {
	void* rip;
	uint64_t cs;
	uint64_t rflags;
	void* rsp;
	uint64_t ss;
};

FERRO_PACKED_STRUCT(fint_frame) {
	farch_int_saved_registers_t saved_registers;
	uint64_t code;
	farch_int_frame_core_t core;
};

// produces a flat view of a frame, useful for macros (because these share the same names as for threads)
FERRO_PACKED_STRUCT(farch_int_frame_flat_registers) {
	uint64_t rax;
	uint64_t rcx;
	uint64_t rdx;
	uint64_t rbx;
	uint64_t rsi;
	uint64_t rdi;
	uint64_t rbp;
	uint64_t r8;
	uint64_t r9;
	uint64_t r10;
	uint64_t r11;
	uint64_t r12;
	uint64_t r13;
	uint64_t r14;
	uint64_t r15;
	uint64_t interrupt_disable;
	uint16_t ds;
	uint16_t es;
	uint16_t fs;
	uint16_t gs;
	uint64_t code;
	void* rip;
	uint64_t cs;
	uint64_t rflags;
	void* rsp;
	uint64_t ss;
};

typedef union farch_int_frame_flat_registers_union {
	fint_frame_t frame;
	farch_int_frame_flat_registers_t flat;
} farch_int_frame_flat_registers_union_t;

/**
 * @}
 */

FERRO_DECLARATIONS_END;

#include <ferro/core/interrupts.h>

#endif // _FERRO_CORE_X86_64_INTERRUPTS_BEFORE_H_
