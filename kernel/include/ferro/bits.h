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
//
// bits.h
//
// a collection of useful bit-manipulation functions, macros, and structures
//

#ifndef _FERRO_BITS_H_
#define _FERRO_BITS_H_

#include <stdint.h>

#include <ferro/base.h>
#include <ferro/platform.h>

FERRO_DECLARATIONS_BEGIN;

// TODO: possibly take advantadge of the compiler's builtin_clz
// the problem with that is that it would have to assume the bit size of types like `unsigned int` and `unsigned long`, which we shouldn't do

#define FERRO_BITS_CLZ_DEFINITION(ferro_suffix, type, bits) \
	FERRO_ALWAYS_INLINE \
	uint8_t ferro_bits_clz_ ## ferro_suffix(type value) { \
		if (value == 0) { \
			return bits; \
		} else { \
			uint8_t count = 0; \
			while ((value & ((type)1 << (bits - 1))) == 0) { \
				++count; \
				value <<= 1; \
			} \
			return count; \
		} \
	};

FERRO_BITS_CLZ_DEFINITION(u8, uint8_t, 8);
FERRO_BITS_CLZ_DEFINITION(u16, uint16_t, 16);
FERRO_BITS_CLZ_DEFINITION(u32, uint32_t, 32);
FERRO_BITS_CLZ_DEFINITION(u64, uint64_t, 64);

#undef FERRO_BITS_CLZ_DEFINITION

#define FERRO_BITS_IN_USE_DEFINITION(ferro_suffix, type, bits) \
	FERRO_ALWAYS_INLINE \
	uint8_t ferro_bits_in_use_ ## ferro_suffix(type value) { \
		return bits - ferro_bits_clz_ ## ferro_suffix(value); \
	};

FERRO_BITS_IN_USE_DEFINITION(u8, uint8_t, 8);
FERRO_BITS_IN_USE_DEFINITION(u16, uint16_t, 16);
FERRO_BITS_IN_USE_DEFINITION(u32, uint32_t, 32);
FERRO_BITS_IN_USE_DEFINITION(u64, uint64_t, 64);

#undef FERRO_BITS_IN_USE_DEFINITION

#define FERRO_BITS_CTZ_DEFINITION(ferro_suffix, type, bits) \
	FERRO_ALWAYS_INLINE \
	uint8_t ferro_bits_ctz_ ## ferro_suffix(type value) { \
		if (value == 0) { \
			return bits; \
		} else { \
			uint8_t count = 0; \
			while ((value & 1) == 0) { \
				++count; \
				value >>= 1; \
			} \
			return count; \
		} \
	};

FERRO_BITS_CTZ_DEFINITION(u8, uint8_t, 8);
FERRO_BITS_CTZ_DEFINITION(u16, uint16_t, 16);
FERRO_BITS_CTZ_DEFINITION(u32, uint32_t, 32);
FERRO_BITS_CTZ_DEFINITION(u64, uint64_t, 64);

#undef FERRO_BITS_CTZ_DEFINITION

#if FERRO_ENDIANNESS == FERRO_ENDIANNESS_BIG
	#define U32_BYTE_ZERO_MASK  0xff000000U
	#define U32_BYTE_ONE_MASK   0x00ff0000U
	#define U32_BYTE_TWO_MASK   0x0000ff00U
	#define U32_BYTE_THREE_MASK 0x000000ffU
#else
	#define U32_BYTE_ZERO_MASK  0x000000ffU
	#define U32_BYTE_ONE_MASK   0x0000ff00U
	#define U32_BYTE_TWO_MASK   0x00ff0000U
	#define U32_BYTE_THREE_MASK 0xff000000U
#endif

FERRO_DECLARATIONS_END;

#endif // _FERRO_BITS_H_
