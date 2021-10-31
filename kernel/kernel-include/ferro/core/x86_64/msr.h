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
 * x86_64 MSR subsystem.
 */

#ifndef _FERRO_CORE_X86_64_MSR_H_
#define _FERRO_CORE_X86_64_MSR_H_

#include <stdint.h>

#include <ferro/base.h>

FERRO_DECLARATIONS_BEGIN;

/**
 * @addtogroup MSR
 *
 * The x86_64 MSR subsystem.
 *
 * @{
 */

FERRO_ENUM(uint64_t, farch_msr) {
	farch_msr_apic_base      =      0x01b,
	farch_msr_tsc_deadline   =      0x6e0,
	farch_msr_efer           = 0xc0000080,
	farch_msr_star           = 0xc0000081,
	farch_msr_lstar          = 0xc0000082,
	farch_msr_cstar          = 0xc0000083,
	farch_msr_sfmask         = 0xc0000084,
	farch_msr_fs_base        = 0xc0000100,
	farch_msr_gs_base        = 0xc0000101,
	farch_msr_gs_base_kernel = 0xc0000102,
};

FERRO_ALWAYS_INLINE uint64_t farch_msr_read(farch_msr_t msr) {
	uint32_t low;
	uint32_t high;
	__asm__ volatile("rdmsr" : "=a" (low), "=d" (high) : "c" (msr));
	return ((uint64_t)high << 32ULL) | (uint64_t)low;
};

FERRO_ALWAYS_INLINE void farch_msr_write(farch_msr_t msr, uint64_t value) {
	uint32_t low = value & 0xffffffffULL;
	uint32_t high = value >> 32ULL;
	__asm__ volatile("wrmsr" :: "a" (low), "d" (high), "c" (msr));
};

/**
 @}
 */

FERRO_DECLARATIONS_END;

#endif // _FERRO_CORE_X86_64_MSR_H_
