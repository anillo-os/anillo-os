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

#ifndef _LIBSYS_X86_64_UCS_H_
#define _LIBSYS_X86_64_UCS_H_

#include <stdint.h>
#include <stddef.h>

#include <libsys/base.h>

LIBSYS_DECLARATIONS_BEGIN;

LIBSYS_STRUCT(sys_ucs_context) {
	//
	// registers that we store to allow us to switch contexts
	//

	uint64_t rip;
	uint64_t rdi;

	//
	// registers that we're required to save
	//

	uint64_t rbx;
	uint64_t rsp;
	uint64_t rbp;
	uint64_t r12;
	uint64_t r13;
	uint64_t r14;
	uint64_t r15;
	uint32_t mxcsr;
	uint16_t x87_cw;
	uint8_t reserved[2];
};

LIBSYS_DECLARATIONS_END;

#endif // _LIBSYS_X86_64_UCS_H_
