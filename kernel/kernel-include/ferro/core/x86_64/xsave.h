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

#ifndef _FERRO_CORE_X86_64_XSAVE_H_
#define _FERRO_CORE_X86_64_XSAVE_H_

#include <stdint.h>

#include <ferro/base.h>
#include <ferro/error.h>

#include <cpuid.h>
#include <immintrin.h>

FERRO_DECLARATIONS_BEGIN;

FERRO_PACKED_STRUCT(farch_xsave_area_legacy) {
	uint8_t _todo[24];
	uint64_t mxcsr;
};

FERRO_PACKED_STRUCT(farch_xsave_header) {
	uint64_t xstate_bv;
	uint64_t xcomp_bv;
};

FERRO_WUR FERRO_ALWAYS_INLINE ferr_t farch_xsave_enable(void) {
	uint64_t cr0 = 0;
	uint64_t cr4 = 0;

	// check whether XSAVE is supported
	unsigned int eax = 0;
	unsigned int ebx = 0;
	unsigned int ecx = 0;
	unsigned int edx = 0;

	// can't use __get_cpuid because it's not always inlined
	__cpuid(1, eax, ebx, ecx, edx);

	if ((ecx & (1 << 26)) == 0) {
		// no XSAVE support
		return ferr_unsupported;
	}

	__asm__ volatile(
		"movq %%cr0, %0\n"
		"movq %%cr4, %1\n"
		:
		"=r" (cr0),
		"=r" (cr4)
	);

	// clear the EM and TS bits
	cr0 &= ~((1 << 2) | (1 << 3));
	// set the NE and MP bits
	cr0 |= (1 << 1) | (1 << 5);

	// enable the OSFXSR, OSXMMEXCEPT, and OSXSAVE bits
	cr4 |= (1 << 9) | (1 << 10) | (1 << 18);

	__asm__ volatile(
		"movq %0, %%cr0\n"
		"movq %1, %%cr4\n"
		::
		"r" (cr0),
		"r" (cr4)
	);

	return ferr_ok;
};

__attribute__((target("xsave")))
FERRO_ALWAYS_INLINE void farch_xsave_init_size_and_mask(uint64_t* out_area_size, uint64_t* out_feature_mask) {
	unsigned int eax;
	unsigned int ebx;
	unsigned int ecx;
	unsigned int edx;
	uint64_t feature_mask;

	__cpuid_count(0x0d, 0, eax, ebx, ecx, edx);

	*out_area_size = ecx;

	feature_mask = ((uint64_t)edx << 32) | (uint64_t)eax;

	// also initialize the XCR0 register with all supported features
	_xsetbv(0, feature_mask);

	*out_feature_mask = feature_mask;
};

FERRO_DECLARATIONS_END;

#endif // _FERRO_CORE_X86_64_XSAVE_H_
