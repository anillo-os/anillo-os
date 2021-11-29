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
 * Per-CPU data subsystem; x86_64 implementations of architecture-specific components.
 */

#ifndef _FERRO_CORE_X86_64_PER_CPU_PRIVATE_H_
#define _FERRO_CORE_X86_64_PER_CPU_PRIVATE_H_

#include <ferro/core/per-cpu.private.h>

FERRO_DECLARATIONS_BEGIN;

FERRO_STRUCT_FWD(fint_frame);
FERRO_STRUCT_FWD(fthread);
FERRO_STRUCT_FWD(futhread_data);

FERRO_STRUCT(farch_per_cpu_data) {
	farch_per_cpu_data_t* base;

	/**
	 * The number of interrupt-disables that have not been balanced with an interrupt-enable.
	 *
	 * Owner: interrupts subsystem.
	 */
	uint64_t outstanding_interrupt_disable_count;

	/**
	 * The TSC's tick rate, in Hz.
	 *
	 * Owner: TSC subsystem.
	 * Also read by: APIC subsystem.
	 */
	uint64_t tsc_frequency;

	/**
	 * The LAPIC timer's tick rate, in Hz.
	 *
	 * Owner: APIC subsystem.
	 */
	uint64_t lapic_frequency;

	/**
	 * The interrupt frame for the currently active/in-progress interrupt.
	 *
	 * Owner: interrupts subsystem.
	 * Also read by: scheduler subsystem.
	 */
	fint_frame_t* current_exception_frame;

	/**
	 * The unique ID assigned to this processor.
	 *
	 * Owner: APIC subsystem.
	 * Also read by: pretty much everything.
	 */
	uint64_t processor_id;

	/**
	 * The thread that is currently executing on this CPU.
	 *
	 * @note In an interrupt context, if a context switch is performed, this will be the thread that will execute when the CPU returns from the interrupt.
	 *
	 * Owner: Officially? The threads subsystem. In reality? The scheduler subsystem.
	 */
	fthread_t* current_thread;

	/**
	 * A place for the temporarily saved rflags register to be stored on syscalls.
	 *
	 * Owner: UThreads (userspace threads) subsystem.
	 */
	uint64_t temporary_rflags;

	/**
	 * The uthread data for the uthread that is currently executing on this CPU.
	 *
	 * Owner: UThreads (userspace threads) subsystem.
	 *
	 * @warning This variable MUST NOT be read or written by ANYONE besides the UThreads subsystem.
	 *          Consider it private for all intents and purposes.
	 *          To obtain a pointer to the current uthread data, look through the hooks in the private thread structure of #current_thread.
	 */
	futhread_data_t* current_uthread_data;

	/**
	 * The main per-CPU data table structure for this CPU. This is used for generic per-CPU data registered at runtime.
	 *
	 * Owner: Per-CPU Data subsystem (the generic one).
	 */
	fper_cpu_main_table_t main_table;

	/**
	 * A small stack used by the scheduler to switch between contexts.
	 * This is a pointer to the top of the stack.
	 *
	 * Owner: scheduler subsystem.
	 */
	void* switching_stack;
};

farch_per_cpu_data_t* farch_per_cpu_base_address(void);

#define FARCH_PER_CPU(_name) (farch_per_cpu_base_address()->_name)

FERRO_ALWAYS_INLINE fper_cpu_main_table_t* fper_cpu_main_table_pointer(void) {
	return &FARCH_PER_CPU(main_table);
};

void farch_per_cpu_init(void);

FERRO_DECLARATIONS_END;

#endif // _FERRO_CORE_X86_64_PER_CPU_PRIVATE_H_
