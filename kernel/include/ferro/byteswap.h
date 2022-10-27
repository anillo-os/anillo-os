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

#ifndef _FERRO_BYTESWAP_H_
#define _FERRO_BYTESWAP_H_

#include <stdint.h>

#include <ferro/base.h>
#include <ferro/platform.h>

FERRO_DECLARATIONS_BEGIN;

FERRO_ALWAYS_INLINE
uint16_t ferro_byteswap_u16(uint16_t value) {
	return (
		((value & 0x00ff) << 8) |
		((value & 0xff00) >> 8)
	);
};

FERRO_ALWAYS_INLINE
uint32_t ferro_byteswap_u32(uint32_t value) {
	return (
		((value & 0x000000ff) << 24) |
		((value & 0x0000ff00) <<  8) |
		((value & 0x00ff0000) >>  8) |
		((value & 0xff000000) >> 24)
	);
};

FERRO_ALWAYS_INLINE
uint64_t ferro_byteswap_u64(uint64_t value) {
	return (
		((value & 0x00000000000000ff) << 56) |
		((value & 0x000000000000ff00) << 40) |
		((value & 0x0000000000ff0000) << 24) |
		((value & 0x00000000ff000000) <<  8) |
		((value & 0x000000ff00000000) >>  8) |
		((value & 0x0000ff0000000000) >> 24) |
		((value & 0x00ff000000000000) >> 40) |
		((value & 0xff00000000000000) >> 56)
	);
};

#if FERRO_ENDIANNESS == FERRO_ENDIANNESS_BIG
	#define ferro_byteswap_native_to_little_u16(value) (ferro_byteswap_u16(value))
	#define ferro_byteswap_native_to_little_u32(value) (ferro_byteswap_u32(value))
	#define ferro_byteswap_native_to_little_u64(value) (ferro_byteswap_u64(value))

	#define ferro_byteswap_little_to_native_u16(value) (ferro_byteswap_u16(value))
	#define ferro_byteswap_little_to_native_u32(value) (ferro_byteswap_u32(value))
	#define ferro_byteswap_little_to_native_u64(value) (ferro_byteswap_u64(value))

	#define ferro_byteswap_native_to_big_u16(value) ((uint16_t)(value))
	#define ferro_byteswap_native_to_big_u32(value) ((uint32_t)(value))
	#define ferro_byteswap_native_to_big_u64(value) ((uint64_t)(value))

	#define ferro_byteswap_big_to_native_u16(value) ((uint16_t)(value))
	#define ferro_byteswap_big_to_native_u32(value) ((uint32_t)(value))
	#define ferro_byteswap_big_to_native_u64(value) ((uint64_t)(value))
#else
	#define ferro_byteswap_native_to_little_u16(value) ((uint16_t)(value))
	#define ferro_byteswap_native_to_little_u32(value) ((uint32_t)(value))
	#define ferro_byteswap_native_to_little_u64(value) ((uint64_t)(value))

	#define ferro_byteswap_little_to_native_u16(value) ((uint16_t)(value))
	#define ferro_byteswap_little_to_native_u32(value) ((uint32_t)(value))
	#define ferro_byteswap_little_to_native_u64(value) ((uint64_t)(value))

	#define ferro_byteswap_native_to_big_u16(value) (ferro_byteswap_u16(value))
	#define ferro_byteswap_native_to_big_u32(value) (ferro_byteswap_u32(value))
	#define ferro_byteswap_native_to_big_u64(value) (ferro_byteswap_u64(value))

	#define ferro_byteswap_big_to_native_u16(value) (ferro_byteswap_u16(value))
	#define ferro_byteswap_big_to_native_u32(value) (ferro_byteswap_u32(value))
	#define ferro_byteswap_big_to_native_u64(value) (ferro_byteswap_u64(value))
#endif

// just for consistency

#define ferro_byteswap_little_to_big_u16(value) (ferro_byteswap_u16(value))
#define ferro_byteswap_little_to_big_u32(value) (ferro_byteswap_u32(value))
#define ferro_byteswap_little_to_big_u64(value) (ferro_byteswap_u64(value))

#define ferro_byteswap_big_to_little_u16(value) (ferro_byteswap_u16(value))
#define ferro_byteswap_big_to_little_u32(value) (ferro_byteswap_u32(value))
#define ferro_byteswap_big_to_little_u64(value) (ferro_byteswap_u64(value))

FERRO_DECLARATIONS_END;

#endif // _FERRO_BYTESWAP_H_
