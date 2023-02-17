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
 * AARCH64 implementation of architecture-specific inline functions for kernel entry.
 */

#ifndef _FERRO_CORE_X86_64_ENTRY_H_
#define _FERRO_CORE_X86_64_ENTRY_H_

#include <ferro/base.h>
#include <ferro/core/entry.h>

FERRO_DECLARATIONS_BEGIN;

/**
 * @addtogroup Kernel-Entry
 *
 * @{
 */

FERRO_ALWAYS_INLINE FERRO_NO_RETURN void fentry_hang_forever(void) {
	while (true) {
		__asm__ volatile(
			// '\043' == '#'
			"msr daifset, \04315\n"
			"wfi\n"
			:::
			"memory"
		);
	}
};

FERRO_ALWAYS_INLINE void fentry_idle(void) {
	__asm__ volatile("wfi" ::: "memory");
};

FERRO_ALWAYS_INLINE void fentry_jump_to_virtual(void* address) {
	__asm__ volatile("br %0" :: "r" (address));
};

/**
 * @}
 */

FERRO_DECLARATIONS_END;

#endif // _FERRO_CORE_X86_64_ENTRY_H_
