/*
 * This file is part of Anillo OS
 * Copyright (C) 2023 Anillo OS Developers
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

#ifndef _FERRO_KASAN_H_
#define _FERRO_KASAN_H_

#include <stdint.h>
#include <stddef.h>

#include <ferro/base.h>

FERRO_DECLARATIONS_BEGIN;

#define FERRO_KASAN_SHADOW_BASE 0xfffffe8000000000ULL
#define FERRO_KASAN_SHADOW_SHIFT 3
#define FERRO_KASAN_SHADOW_SCALE (1ull << FERRO_KASAN_SHADOW_SHIFT)
#define FERRO_KASAN_SHADOW_DELTA 0xe0000e8000000000ULL

FERRO_ALWAYS_INLINE uintptr_t ferro_kasan_shadow_for_pointer(uintptr_t pointer) {
	return FERRO_KASAN_SHADOW_DELTA + (pointer >> FERRO_KASAN_SHADOW_SHIFT);
};

void ferro_kasan_poison(uintptr_t pointer, size_t size);
void ferro_kasan_unpoison(uintptr_t pointer, size_t size);
void ferro_kasan_clean(uintptr_t pointer, size_t size);
void ferro_kasan_check(uintptr_t pointer, size_t size);

void ferro_kasan_load_unchecked(const void* pointer, size_t size, void* out_value);
void ferro_kasan_store_unchecked(void* pointer, size_t size, const void* value);

#define ferro_kasan_load_unchecked_auto(_ptr) ({ \
		__typeof__(*(_ptr)) _result; \
		ferro_kasan_load_unchecked((_ptr), sizeof(_result), &_result); \
		_result; \
	})

#define ferro_kasan_store_unchecked_auto(_ptr, _val) do { \
		__typeof__(*(_ptr)) _tmp = (_val); \
		ferro_kasan_store_unchecked((_ptr), sizeof(_tmp), &_tmp); \
	} while (0);

void ferro_kasan_copy_unchecked(void* destination, const void* source, size_t size);
void ferro_kasan_fill_unchecked(void* destination, uint8_t value, size_t size);

FERRO_DECLARATIONS_END;

#endif // _FERRO_KASAN_H_
