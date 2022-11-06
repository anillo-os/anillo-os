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

#ifndef _FERRO_CORE_X86_64_CPU_PRIVATE_H_
#define _FERRO_CORE_X86_64_CPU_PRIVATE_H_

#include <ferro/core/cpu.h>
#include <ferro/core/x86_64/per-cpu.private.h>

FERRO_DECLARATIONS_BEGIN;

FERRO_OPTIONS(uint64_t, farch_cpu_flags) {
	farch_cpu_flag_usable = 1 << 0,
	farch_cpu_flag_online = 1 << 1,
};

FERRO_STRUCT(fcpu) {
	farch_cpu_flags_t flags;
	farch_per_cpu_data_t* per_cpu_data;
	uint64_t apic_id;
};

FERRO_DECLARATIONS_END;

#endif // _FERRO_CORE_X86_64_CPU_PRIVATE_H_
