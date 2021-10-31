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
 * Per-CPU data subsystem.
 */

#ifndef _FERRO_CORE_PER_CPU_PRIVATE_H_
#define _FERRO_CORE_PER_CPU_PRIVATE_H_

#include <stddef.h>

#include <ferro/core/per-cpu.h>

FERRO_DECLARATIONS_BEGIN;

FERRO_OPTIONS(uint32_t, fper_cpu_entry_flags) {
	fper_cpu_entry_is_registered  = 1 << 0,
	fper_cpu_entry_flag_has_value = 1 << 1,
};

typedef uint32_t fper_cpu_small_key_t;

FERRO_STRUCT(fper_cpu_entry) {
	fper_cpu_small_key_t key;
	fper_cpu_entry_flags_t flags;
	fper_cpu_data_t data;
	fper_cpu_data_destructor_f destructor;
	void* destructor_context;
};

FERRO_STRUCT(fper_cpu_main_table) {
	fper_cpu_entry_t* entries;
	size_t entry_count;
};

void fper_cpu_init(void);

// these are architecture-dependent functions that we expect all architectures to implement

FERRO_ALWAYS_INLINE fper_cpu_main_table_t* fper_cpu_main_table_pointer(void);

FERRO_DECLARATIONS_END;

// include the arch-dependent after-header
#if FERRO_ARCH == FERRO_ARCH_x86_64
	#include <ferro/core/x86_64/per-cpu.private.h>
#elif FERRO_ARCH == FERRO_ARCH_aarch64
	#include <ferro/core/aarch64/per-cpu.private.h>
#else
	#error Unrecognized/unsupported CPU architecture! (see <ferro/core/per-cpu.private.h>)
#endif

#endif // _FERRO_CORE_PER_CPU_PRIVATE_H_
