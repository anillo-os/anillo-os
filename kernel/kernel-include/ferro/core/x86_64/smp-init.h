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

#ifndef _FERRO_CORE_X86_64_SMP_INIT_H_
#define _FERRO_CORE_X86_64_SMP_INIT_H_

#include <ferro/base.h>

/**
 * The address where we place our AP (application processor) start-up code.
 *
 * This address choice is somewhat arbitrary. However, it is close to the typical real-mode
 * initialization address of 0x7c40 and stays low enough not to mess with any important memory
 * addresses in low-memory (e.g. like the VGA base address).
 */
#define FARCH_SMP_INIT_BASE 0x8000

/**
 * The address where we place data needed for AP initialization.
 */
#define FARCH_SMP_INIT_DATA_BASE 0x9000

/**
 * The address where we place the stubbed root page table for AP initialization.
 */
#define FARCH_SMP_INIT_ROOT_TABLE_BASE 0xa000
#define FARCH_SMP_INIT_P3_TABLE_BASE 0xb000
#define FARCH_SMP_INIT_P2_TABLE_BASE 0xc000
#define FARCH_SMP_INIT_P1_TABLE_BASE 0xd000

/**
 * How big the initial stack for each CPU should be (in bytes).
 */
#if FERRO_ASSEMBLER
	#define FARCH_SMP_INIT_STACK_SIZE (2 * 1024 * 1024)
#else
	#define FARCH_SMP_INIT_STACK_SIZE (2ull * 1024 * 1024)
#endif

#if !FERRO_ASSEMBLER

#include <stdint.h>

#include <ferro/core/interrupts.h>
#include <ferro/core/paging.h>

FERRO_STRUCT(farch_smp_init_data) {
	farch_int_gdt_legacy_pointer_t gdt_pointer;
	uint16_t padding0;
	farch_int_idt_legacy_pointer_t idt_pointer;
	uint16_t padding1;
	farch_int_gdt_t gdt;
	void* stack;
	uint64_t apic_id;
	uint8_t init_done;
};

FERRO_VERIFY(sizeof(farch_smp_init_data_t) <= FPAGE_PAGE_SIZE, "SMP init data must fit within a single page");

extern char farch_smp_init_code_start;
extern char farch_smp_init_code_end;

#endif

#endif // _FERRO_CORE_X86_64_SMP_INIT_H_
