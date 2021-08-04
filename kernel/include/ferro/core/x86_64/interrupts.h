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

#ifndef _FERRO_CORE_X86_64_INTERRUPTS_H_
#define _FERRO_CORE_X86_64_INTERRUPTS_H_

#include <ferro/base.h>

#include <ferro/core/interrupts.h>

FERRO_DECLARATIONS_BEGIN;

FERRO_ALWAYS_INLINE void fint_disable(void) {
	__asm__ volatile("cli" ::: "memory");
};

FERRO_ALWAYS_INLINE void fint_enable(void) {
	__asm__ volatile("sti" ::: "memory");
};

FERRO_ALWAYS_INLINE uint64_t farch_save_flags(void) {
	uint64_t flags = 0;

	__asm__ volatile(
		"pushfq\n"
		"pop %0\n"
		:
		"=rm" (flags)
		::
		"memory"
	);

	return flags;
};

FERRO_ALWAYS_INLINE uint64_t fint_save(void) {
	return farch_save_flags() & 0x200ULL;
};

FERRO_ALWAYS_INLINE void fint_restore(uint64_t state) {
	if (state & 0x200ULL) {
		fint_enable();
	} else {
		fint_disable();
	}
};

FERRO_DECLARATIONS_END;

#endif // _FERRO_CORE_X86_64_INTERRUPTS_H_
