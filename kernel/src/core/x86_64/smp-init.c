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

#include <ferro/core/x86_64/smp-init.h>
#include <ferro/core/x86_64/xsave.h>
#include <ferro/core/x86_64/msr.h>
#include <ferro/core/cpu.private.h>
#include <libsimple/general.h>
#include <ferro/core/paging.private.h>
#include <ferro/core/scheduler.h>
#include <ferro/core/x86_64/apic.h>

extern uint64_t farch_apic_processors_online;

// interrupts are disabled on entry
FERRO_NO_RETURN
__attribute__((target("xsave")))
void farch_smp_init_entry(farch_smp_init_data_t* init_data) {
	__atomic_store_n(&init_data->init_done, 1, __ATOMIC_RELEASE);

	// enable xsave (and other SIMD instructions) on this CPU
	if (farch_xsave_enable() != ferr_ok) {
		fpanic(NULL);
	}

	// set up the hidden FS and GS registers
	// (we already cleared the visible FS and GS registers to 0 in `smp-init.S`)
	farch_msr_write(farch_msr_fs_base, 0);
	farch_msr_write(farch_msr_gs_base, 0);
	farch_msr_write(farch_msr_gs_base_kernel, (uintptr_t)init_data->cpu_info_struct->per_cpu_data);

	// perform an initial swapgs to get the correct gs for kernel-space
	__asm__ volatile("swapgs" ::: "cc", "memory");

	// initialize the per-CPU data base pointer
	// (this is necessary to be able to access the per-CPU data via GS-relative addressing)
	init_data->cpu_info_struct->per_cpu_data->base = init_data->cpu_info_struct->per_cpu_data;

	// initialize miscellaneous per-CPU data
	//
	// note that the per-CPU data structure has already been zeroed out, so we can skip initializing
	// variables that only need to be initialized to zero.
	FARCH_PER_CPU(outstanding_interrupt_disable_count) = 1;
	FARCH_PER_CPU(tsc_frequency) = init_data->tsc_frequency;
	FARCH_PER_CPU(lapic_frequency) = init_data->lapic_frequency;
	FARCH_PER_CPU(processor_id) = init_data->cpu_info_struct->apic_id;

	// TODO: initialize the generic per-CPU table

	FARCH_PER_CPU(address_space) = fpage_space_kernel();

	// initialize the xsave area size and feature mask variables
	farch_xsave_init_size_and_mask(&FARCH_PER_CPU(xsave_area_size), &FARCH_PER_CPU(xsave_features));

	FARCH_PER_CPU(current_cpu) = init_data->cpu_info_struct;

	//
	// initialize paging on this processor
	//

	// first, copy the current (temporary) root table to the new (final) root table
	simple_memcpy(init_data->cpu_info_struct->root_table, (const void*)fpage_virtual_address_for_table(0, 0, 0, 0), sizeof(*init_data->cpu_info_struct->root_table));

	// next, update the recursive table pointer
	init_data->cpu_info_struct->root_table->entries[fpage_root_recursive_index] = fpage_table_entry(fpage_virtual_to_physical((uintptr_t)init_data->cpu_info_struct->root_table), true);

	// now switch to that table
	__asm__ volatile("mov %0, %%cr3" :: "r" (fpage_virtual_to_physical((uintptr_t)init_data->cpu_info_struct->root_table)) : "memory");

	// and finally, perform other necessary initialization in the paging subsystem
	fpage_init_secondary_cpu();

	// now initialize interrupts on this CPU
	fint_init_secondary_cpu();

	// initialize the APIC for this CPU
	farch_apic_init_secondary_cpu();

	// we're online now, so mark ourselves as such
	__atomic_add_fetch(&farch_apic_processors_online, 1, __ATOMIC_RELAXED);

	// use `__ATOMIC_RELEASE` to ensure that the BSP (and any other APs) can see all the writes we performed during init stage 2
	__atomic_store_n(&init_data->init_stage2_done, 1, __ATOMIC_RELEASE);

	// finally, jump into the scheduler
	fsched_init_secondary_cpu();
};
