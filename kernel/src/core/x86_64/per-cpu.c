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
 * x86_64 implementation of per-CPU data.
 */

#include <ferro/core/per-cpu.private.h>
#include <ferro/core/x86_64/msr.h>
#include <ferro/core/interrupts.h>

// for now, we only ever operate on a single CPU
// however, once we enable SMP, we can extend this

static farch_per_cpu_data_t data = {
	.base = &data,
};

static farch_int_gdt_t gdt = {0};

// this function MUST be called before the interrupts subsystem is initialized
// (because it needs to use a temporary GDT)
void farch_per_cpu_init(void) {
	farch_int_gdt_pointer_t gdt_pointer;

	gdt_pointer.limit = sizeof(gdt) - 1;
	gdt_pointer.base = &gdt;
	__asm__ volatile(
		"lgdt (%0)"
		::
		"r" (&gdt_pointer)
		:
		"memory"
	);

	__asm__ volatile(
		// load fs and gs segment registers with null
		"movw %0, %%fs\n"
		"movw %0, %%gs\n"
		::
		"r" (0)
		:
		"memory"
	);

	// now write to the hidden registers
	// fs and gs should NOT be modified after this point, because
	// on some CPUs (*cough* Intel *cough*), reloading fs and gs clears the hidden registers
	farch_msr_write(farch_msr_fs_base, 0);
	farch_msr_write(farch_msr_gs_base, 0);
	farch_msr_write(farch_msr_gs_base_kernel, (uintptr_t)&data);

	// perform an initial swapgs to get the correct gs for kernel-space
	__asm__ volatile("swapgs" ::: "cc", "memory");
};

farch_per_cpu_data_t* farch_per_cpu_base_address(void) {
	return ((farch_per_cpu_data_t FERRO_GS_RELATIVE*)NULL)->base;
};
