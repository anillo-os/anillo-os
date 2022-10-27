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
 * x86_64 implementations of architecture-specific components for the paging subsystem.
 */

#ifndef _FERRO_CORE_X86_64_PAGING_H_
#define _FERRO_CORE_X86_64_PAGING_H_

#include <stddef.h>
#include <stdint.h>

#include <ferro/base.h>

#include <ferro/core/paging.h>

FERRO_DECLARATIONS_BEGIN;

FERRO_ALWAYS_INLINE bool fpage_address_is_canonical(uintptr_t virtual_address) {
	uint16_t high_16 = virtual_address >> 48;
	uint16_t expected = (virtual_address & (1ull << 47)) ? 0xffff : 0;
	return high_16 == expected;
};

FERRO_DECLARATIONS_END;

#endif // _FERRO_CORE_X86_64_PAGING_H_
