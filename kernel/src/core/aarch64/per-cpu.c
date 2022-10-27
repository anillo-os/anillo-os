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
 * AARCH64 implementation of per-CPU data.
 */

#include <ferro/core/per-cpu.private.h>

// for now, we only ever operate on a single CPU
// however, once we enable SMP, we can extend this

static farch_per_cpu_data_t data = {
	.base = &data,
};

farch_per_cpu_data_t* farch_per_cpu_base_address(void) {
	uint64_t addr = 0;
	__asm__ volatile("mrs %0, tpidr_el1" : "=r" (addr));
	return (void*)addr;
};

uint64_t fcpu_id(void) {
	return 0;
};

uint64_t fcpu_count(void) {
	return 1;
};

void farch_per_cpu_init(void) {
	__asm__ volatile("msr tpidr_el1, %0" :: "r" (&data));
};
