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

#ifndef _FERRO_ASM_AARCH64_HELPERS_HS_
#define _FERRO_ASM_AARCH64_HELPERS_HS_

#include <ferro/asm/common.hS>

#define MAKE_IMMEDIATE_HELPER(x, y) x ## y
#define MAKE_IMMEDIATE_HELPER2(x, y) MAKE_IMMEDIATE_HELPER(x, y)
#define MAKE_IMMEDIATE_HASH #
#define MAKE_IMMEDIATE(x) MAKE_IMMEDIATE_HELPER2(MAKE_IMMEDIATE_HASH, x)

#endif // _FERRO_ASM_AARCH64_HELPERS_HS_
