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

#ifndef _FERRO_PLATFORM_H_
#define _FERRO_PLATFORM_H_

// CPU architecture
#define FERRO_ARCH_x86_64 1
#define FERRO_ARCH_aarch64  2

#if defined(__x86_64__)
	#define FERRO_ARCH FERRO_ARCH_x86_64
#elif defined(__aarch64__)
	#define FERRO_ARCH FERRO_ARCH_aarch64
#else
	#error Unrecognized/unsupported CPU architecture! (See <ferro/platform.h>)
#endif

// CPU endianness
#define FERRO_ENDIANNESS_BIG    1
#define FERRO_ENDIANNESS_LITTLE 2

#if (defined(__BYTE_ORDER) && __BYTE_ORDER == __BIG_ENDIAN) || (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__) || defined(__BIG_ENDIAN__) || defined(__ARMEB__) || defined(__THUMBEB__) || defined(__AARCH64EB__)
	#define FERRO_ENDIANNESS FERRO_ENDIANNESS_BIG
#elif (defined(__BYTE_ORDER) && __BYTE_ORDER == __LITTLE_ENDIAN) || (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__) || defined(__LITTLE_ENDIAN__) || defined(__ARMEL__) || defined(__THUMBEL__) || defined(__AARCH64EL__)
	#define FERRO_ENDIANNESS FERRO_ENDIANNESS_LITTLE
#else
	#error Unrecognized/unsupported CPU endianness! (See <ferro/platform.h>)
#endif

// CPU bitness
#define FERRO_BITNESS_64 1

#if FERRO_ARCH == FERRO_ARCH_x86_64 || FERRO_ARCH == FERRO_ARCH_aarch64
	#define FERRO_BITNESS FERRO_BITNESS_64
#else
	#error Unrecognized/unsupported CPU bitness! (See <ferro/platform.h>)
#endif

#endif // _FERRO_PLATFORM_H_
