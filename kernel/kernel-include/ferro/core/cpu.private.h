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

#ifndef _FERRO_CORE_CPU_PRIVATE_H_
#define _FERRO_CORE_CPU_PRIVATE_H_

// include the arch-dependent header
#if FERRO_ARCH == FERRO_ARCH_x86_64
	#include <ferro/core/x86_64/cpu.private.h>
#elif FERRO_ARCH == FERRO_ARCH_aarch64
	#include <ferro/core/aarch64/cpu.private.h>
#else
	#error Unrecognized/unsupported CPU architecture! (see <ferro/core/cpu.private.h>)
#endif

#endif // _FERRO_CORE_CPU_PRIVATE_H_
