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
 * x86_64 TSC subsystem.
 */

#ifndef _FERRO_CORE_X86_64_TSC_H_
#define _FERRO_CORE_X86_64_TSC_H_

#include <stdint.h>

#include <ferro/base.h>
#include <ferro/core/x86_64/per-cpu.h>

#include <x86intrin.h>

FERRO_DECLARATIONS_BEGIN;

/**
 * @addtogroup TSC
 *
 * The x86_64 TSC subsystem.
 *
 * @{
 */

/**
 * Initializes the TSC subsystem.
 */
void farch_tsc_init(void);

FERRO_ALWAYS_INLINE uint64_t farch_tsc_read_weak(void) {
	return __rdtsc();
};

FERRO_ALWAYS_INLINE uint64_t farch_tsc_read(void) {
	uint64_t value;
	//_mm_mfence();
	value = farch_tsc_read_weak();
	_mm_lfence();
	return value;
};

/**
 * Converts the given number of nanoseconds into a TSC offset.
 *
 * When the TSC value reaches `current TSC + offset`, the given number of nanoseconds will have elapsed.
 */
FERRO_ALWAYS_INLINE uint64_t farch_tsc_ns_to_offset(uint64_t ns) {
	// this is terribly unoptimized, but let's trust the compiler to do the right thing
	__uint128_t tmp = ns;

	tmp *= FARCH_PER_CPU(tsc_frequency);

	// there are 1e9 nanoseconds in a second
	tmp /= 1000000000ULL;

	return (uint64_t)tmp;
};

/**
 * Converts the given TSC offset into a number of nanoseconds.
 *
 * When the returned number of nanoseconds have elapsed, the TSC value will have reached `current TSC + offset`.
 */
FERRO_ALWAYS_INLINE uint64_t farch_tsc_offset_to_ns(uint64_t offset) {
	// again, terribly unoptimized
	__uint128_t tmp = offset;

	tmp *= 1000000000ULL;

	tmp /= FARCH_PER_CPU(tsc_frequency);

	return (uint64_t)tmp;
};

/**
 * @}
 */

FERRO_DECLARATIONS_END;

#endif // _FERRO_CORE_X86_64_TSC_H_
