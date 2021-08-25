/**
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

#ifndef _FERRO_CORE_AARCH64_THREADS_BEFORE_H_
#define _FERRO_CORE_AARCH64_THREADS_BEFORE_H_

#include <stdint.h>

#include <ferro/base.h>

FERRO_DECLARATIONS_BEGIN;

FERRO_STRUCT(fthread_saved_context) {
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
	uint64_t pc;
	uint64_t sp;

	// actually spsr
	uint64_t pstate;

	uint16_t interrupt_disable;
	uint64_t reserved;
};

// needs to be 16-byte aligned so we can push it onto the stack
FERRO_VERIFY_ALIGNMENT(fthread_saved_context_t, 16);

FERRO_OPTIONS(uint64_t, farch_thread_pstate) {
	farch_thread_pstate_negative          = 1ULL << 31,
	farch_thread_pstate_zero              = 1ULL << 30,
	farch_thread_pstate_carry             = 1ULL << 29,
	farch_thread_pstate_overflow          = 1ULL << 28,
	farch_thread_pstate_tco               = 1ULL << 25,
	farch_thread_pstate_dit               = 1ULL << 24,
	farch_thread_pstate_uao               = 1ULL << 23,
	farch_thread_pstate_pan               = 1ULL << 22,
	farch_thread_pstate_software_step     = 1ULL << 21,
	farch_thread_pstate_illegal_execution = 1ULL << 20,
	farch_thread_pstate_ssbs              = 1ULL << 12,
	farch_thread_pstate_debug_mask        = 1ULL << 9,
	farch_thread_pstate_serror_mask       = 1ULL << 8,
	farch_thread_pstate_irq_mask          = 1ULL << 7,
	farch_thread_pstate_fiq_mask          = 1ULL << 6,
	farch_thread_pstate_aarch64           = 0ULL << 4,
	farch_thread_pstate_el1               = 1ULL << 2,
	farch_thread_pstate_el0               = 0ULL << 2,
	farch_thread_pstate_spx               = 1ULL << 0,
	farch_thread_pstate_sp0               = 0ULL << 0,
};

FERRO_DECLARATIONS_END;

#include <ferro/core/threads.h>

#endif // _FERRO_CORE_AARCH64_THREADS_BEFORE_H_
