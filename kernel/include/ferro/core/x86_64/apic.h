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
 * x86_64 APIC subsystem.
 *
 * Provides 2 backends for timers subsystem.
 */

#ifndef _FERRO_CORE_X86_64_APIC_H_
#define _FERRO_CORE_X86_64_APIC_H_

#include <ferro/base.h>
#include <ferro/core/x86_64/per-cpu.h>

FERRO_DECLARATIONS_BEGIN;

/**
 * @addtogroup APIC
 *
 * The x86_64 APIC subsystem.
 *
 * @{
 */

/**
 * Initializes the APIC subsystem.
 */
void farch_apic_init(void);

/**
 * Converts the given number of nanoseconds into a number of APIC timer cycles (with a divider of 1).
 */
FERRO_ALWAYS_INLINE uint64_t farch_apic_timer_ns_to_cycles(uint64_t ns) {
	// this is terribly unoptimized, but let's trust the compiler to do the right thing
	__uint128_t tmp = ns;

	tmp *= FARCH_PER_CPU(lapic_frequency);

	// there are 1e9 nanoseconds in a second
	tmp /= 1000000000ULL;

	return (uint64_t)tmp;
};

/**
 * Converts the given number of APIC timer cycles into a number of nanoseconds.
 */
FERRO_ALWAYS_INLINE uint64_t farch_apic_timer_cycles_to_ns(uint64_t offset) {
	// again, terribly unoptimized
	__uint128_t tmp = offset;

	tmp *= 1000000000ULL;

	tmp /= FARCH_PER_CPU(lapic_frequency);

	return (uint64_t)tmp;
};

/**
 * @}
 */

FERRO_DECLARATIONS_END;

#endif // _FERRO_CORE_X86_64_APIC_H_
