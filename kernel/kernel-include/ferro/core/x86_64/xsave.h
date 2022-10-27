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

FERRO_DECLARATIONS_BEGIN;

FERRO_PACKED_STRUCT(farch_xsave_area_legacy) {
	uint8_t _todo[24];
	uint64_t mxcsr;
};

FERRO_PACKED_STRUCT(farch_xsave_header) {
	uint64_t xstate_bv;
	uint64_t xcomp_bv;
};

FERRO_DECLARATIONS_END;

#endif // _FERRO_CORE_X86_64_XSAVE_H_
