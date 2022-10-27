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

#ifndef _LIBSYS_AARCH64_UCS_H_
#define _LIBSYS_AARCH64_UCS_H_

#include <stdint.h>
#include <stddef.h>

#include <libsys/base.h>

LIBSYS_DECLARATIONS_BEGIN;

LIBSYS_STRUCT(sys_ucs_context) {
	//
	// registers that we save to allow us to switch contexts
	//

	uint64_t ip;
	uint64_t x0;

	//
	// registers that we're required to save
	//

	uint64_t x19;
	uint64_t x20;
	uint64_t x21;
	uint64_t x22;
	uint64_t x23;
	uint64_t x24;
	uint64_t x25;
	uint64_t x26;
	uint64_t x27;
	uint64_t x28;
	uint64_t x29;
	uint64_t x30; // a.k.a. lr
	uint64_t sp;
	uint64_t fpcr;
	// for the AAPCS64, we only need to preserve the bottom 64 bits of v8-v15 (a.k.a. d8-d15)
	uint64_t fp_registers[8];
};

LIBSYS_DECLARATIONS_END;

#endif // _LIBSYS_AARCH64_UCS_H_
