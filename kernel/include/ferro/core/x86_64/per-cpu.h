/**
 * This file is part of Anillo OS
 * Copyright (C) 2020 Anillo OS Developers
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

#ifndef _FERRO_CORE_X86_64_PER_CPU_H_
#define _FERRO_CORE_X86_64_PER_CPU_H_

#include <stdint.h>
#include <stddef.h>

#include <ferro/base.h>

FERRO_DECLARATIONS_BEGIN;

FERRO_STRUCT_FWD(farch_int_isr_frame);
FERRO_STRUCT_FWD(fthread);

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
	farch_int_isr_frame_t* current_exception_frame;

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
};

farch_per_cpu_data_t* farch_per_cpu_base_address(void);

#define FARCH_PER_CPU_TYPEOF(_name) __typeof__(((farch_per_cpu_data_t*)NULL)->_name)
#define FARCH_PER_CPU(_name) (farch_per_cpu_base_address()->_name)

FERRO_DECLARATIONS_END;

#endif // _FERRO_CORE_X86_64_PER_CPU_H_
