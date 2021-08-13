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

#ifndef _FERRO_CORE_AARCH64_GENERIC_TIMER_H_
#define _FERRO_CORE_AARCH64_GENERIC_TIMER_H_

#include <stdint.h>

#include <ferro/base.h>

FERRO_DECLARATIONS_BEGIN;

/**
 * Initializes the AARCH64 Generic Timer subsystem.
 */
void farch_generic_timer_init(void);

FERRO_ALWAYS_INLINE uint64_t farch_generic_timer_read_frequency(void) {
	uint64_t result;
	__asm__ volatile("mrs %0, cntfrq_el0" : "=r" (result));
	return result;
};

FERRO_ALWAYS_INLINE uint64_t farch_generic_timer_ns_to_offset(uint64_t ns) {
	// like x86_64's TSC, this is severely unoptimized
	__uint128_t tmp = ns;
	tmp *= farch_generic_timer_read_frequency();
	tmp /= 1000000000ULL;
	return (uint64_t)tmp;
};

FERRO_ALWAYS_INLINE uint64_t farch_generic_timer_offset_to_ns(uint64_t offset) {
	// ditto
	__uint128_t tmp = offset;
	tmp *= 1000000000ULL;
	tmp /= farch_generic_timer_read_frequency();
	return (uint64_t)tmp;
};

FERRO_ALWAYS_INLINE uint64_t farch_generic_timer_read_counter_weak(void) {
	uint64_t result;
	__asm__ volatile("mrs %0, cntvct_el0" : "=r" (result));
	return result;
};

FERRO_DECLARATIONS_END;

#endif // _FERRO_CORE_AARCH64_GENERIC_TIMER_H_
