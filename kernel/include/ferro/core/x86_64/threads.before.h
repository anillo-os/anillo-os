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
 * x86_64 implementations of architecture-specific components for threads subsystem; before-header.
 */

#ifndef _FERRO_CORE_X86_64_THREADS_BEFORE_H_
#define _FERRO_CORE_X86_64_THREADS_BEFORE_H_

#include <stdint.h>

#include <ferro/base.h>

FERRO_DECLARATIONS_BEGIN;

/**
 * @addtogroup Threads
 *
 * @{
 */

FERRO_STRUCT(fthread_saved_context) {
	//
	// general purpose registers
	//
	uint64_t rax;
	uint64_t rcx;
	uint64_t rdx;
	uint64_t rbx;
	uint64_t rsi;
	uint64_t rdi;
	uint64_t rsp;
	uint64_t rbp;
	uint64_t r8;
	uint64_t r9;
	uint64_t r10;
	uint64_t r11;
	uint64_t r12;
	uint64_t r13;
	uint64_t r14;
	uint64_t r15;

	//
	// special registers
	//
	uint64_t rip;
	uint64_t rflags;
	uint64_t cs;
	uint64_t ss;

	// the per-CPU interrupt-disable count
	uint64_t interrupt_disable;

	// some struct packing/space optimization by placing these 4 `uint16_t`s together
	uint16_t ds;
	uint16_t es;
	uint16_t fs;
	uint16_t gs;
};

/**
 * @}
 */

FERRO_DECLARATIONS_END;

#include <ferro/core/threads.h>

#endif // _FERRO_CORE_X86_64_THREADS_BEFORE_H_
