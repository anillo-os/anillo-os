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

#include <stdbool.h>
#include <stddef.h>

#include <ferro/kasan.h>
#include <ferro/core/console.h>
#include <ferro/core/panic.h>
#include <libsimple/libsimple.h>

// based on https://github.com/managarm/managarm/blob/cd4d5c20111a3b0fa09b770d70fc95e620867e69/kernel/thor/generic/kasan.cpp

static void handle_report(bool write, uintptr_t bad_address, size_t size, void* bad_instruction) {
	fconsole_logf("KASan %s error at IP %p with %lu byte(s) at %p\n", write ? "write" : "read", bad_instruction, size, (void*)bad_address);
	fpanic("KASan error");
};

#define ASAN_REPORT_SIZE(_size) \
	void __asan_report_load ## _size ## _noabort(uintptr_t address) { \
		handle_report(false, address, _size, __builtin_return_address(0)); \
	}; \
	void __asan_report_store ## _size ## _noabort(uintptr_t address) { \
		handle_report(true, address, _size, __builtin_return_address(0)); \
	};

ASAN_REPORT_SIZE(1);
ASAN_REPORT_SIZE(2);
ASAN_REPORT_SIZE(4);
ASAN_REPORT_SIZE(8);
ASAN_REPORT_SIZE(16);

void __asan_report_load_n_noabort(uintptr_t address, size_t size) {
	handle_report(false, address, size, __builtin_return_address(0));
};

void __asan_report_store_n_noabort(uintptr_t address, size_t size) {
	handle_report(true, address, size, __builtin_return_address(0));
};

void __asan_handle_no_return() {
	// do nothing
};

void __asan_alloca_poison(uintptr_t address, size_t size) {
	// TODO
};

void __asan_allocas_unpoison(void* stack_top, void* stack_bottom) {
	// TODO
};

void ferro_kasan_poison(uintptr_t pointer, size_t size) {
#if FERRO_KASAN
	uint8_t* shadow = (void*)ferro_kasan_shadow_for_pointer(pointer);
	size_t size_shift = size >> FERRO_KASAN_SHADOW_SHIFT;
	size_t size_remainder = size & (FERRO_KASAN_SHADOW_SCALE - 1);

	for (size_t n = 0; n < size_shift; ++n) {
		fassert(shadow[n] == 0);
		shadow[n] = 0xff;
	}

	if (size_remainder != 0) {
		size_t n = size_shift;
		fassert(shadow[n] == size_remainder);
		shadow[n] = 0xff;
	}
#endif
};

void ferro_kasan_unpoison(uintptr_t pointer, size_t size) {
#if FERRO_KASAN
	uint8_t* shadow = (void*)ferro_kasan_shadow_for_pointer(pointer);
	size_t size_shift = size >> FERRO_KASAN_SHADOW_SHIFT;
	size_t size_remainder = size & (FERRO_KASAN_SHADOW_SCALE - 1);

	for (size_t n = 0; n < size_shift; ++n) {
		fassert(shadow[n] == 0xff);
		shadow[n] = 0;
	}

	if (size_remainder != 0) {
		size_t n = size_shift;
		fassert(shadow[n] == 0xff);
		shadow[n] = size_remainder;
	}
#endif
};

void ferro_kasan_clean(uintptr_t pointer, size_t size) {
#if FERRO_KASAN
	uint8_t* shadow = (void*)ferro_kasan_shadow_for_pointer(pointer);
	size_t size_shift = size >> FERRO_KASAN_SHADOW_SHIFT;
	size_t size_remainder = size & (FERRO_KASAN_SHADOW_SCALE - 1);

	for (size_t n = 0; n < size_shift; ++n) {
		shadow[n] = 0;
	}

	if (size_remainder != 0) {
		size_t n = size_shift;
		shadow[n] = size_remainder;
	}
#endif
};

void ferro_kasan_check(uintptr_t pointer, size_t size) {
#if FERRO_KASAN
	uint8_t* shadow = (void*)ferro_kasan_shadow_for_pointer(pointer);
	size_t size_shift = size >> FERRO_KASAN_SHADOW_SHIFT;
	size_t size_remainder = size & (FERRO_KASAN_SHADOW_SCALE - 1);

	for (size_t n = 0; n < size_shift; ++n) {
		fassert(shadow[n] == 0);
	}
#endif
};

FERRO_NO_KASAN
void ferro_kasan_load_unchecked(const void* pointer, size_t size, void* out_value) {
	ferro_kasan_copy_unchecked(out_value, pointer, size);
};

FERRO_NO_KASAN
void ferro_kasan_store_unchecked(void* pointer, size_t size, const void* value) {
	ferro_kasan_copy_unchecked(pointer, value, size);
};

FERRO_NO_KASAN
void ferro_kasan_copy_unchecked(void* destination, const void* source, size_t size) {
#if FERRO_KASAN
	char* dest = destination;
	const char* src = source;

	if (FERRO_IS_ALIGNED((uintptr_t)destination, sizeof(uint64_t)) && FERRO_IS_ALIGNED((uintptr_t)source, sizeof(uint64_t))) {
		while (size > sizeof(uint64_t)) {
			*(uint64_t*)dest = *(const uint64_t*)src;
			dest += sizeof(uint64_t);
			src  += sizeof(uint64_t);
			size -= sizeof(uint64_t);
		}
	}

	while (size > 0) {
		*(uint8_t*)dest = *(const uint8_t*)src;
		dest += sizeof(uint8_t);
		src  += sizeof(uint8_t);
		size -= sizeof(uint8_t);
	}
#else
	simple_memcpy(destination, source, size);
#endif
};

FERRO_NO_KASAN
void ferro_kasan_fill_unchecked(void* destination, uint8_t value, size_t size) {
#if FERRO_KASAN
	char* dest = destination;

	if (FERRO_IS_ALIGNED((uintptr_t)destination, sizeof(uint64_t))) {
		uint64_t val64 =
			((uint64_t)value <<  0) |
			((uint64_t)value <<  8) |
			((uint64_t)value << 16) |
			((uint64_t)value << 24) |
			((uint64_t)value << 32) |
			((uint64_t)value << 40) |
			((uint64_t)value << 48) |
			((uint64_t)value << 56) ;

		while (size > sizeof(uint64_t)) {
			*(uint64_t*)dest = val64;
			dest += sizeof(uint64_t);
			size -= sizeof(uint64_t);
		}
	}

	while (size > 0) {
		*(uint8_t*)dest = value;
		dest += sizeof(uint8_t);
		size -= sizeof(uint8_t);
	}
#else
	simple_memset(destination, value, size);
#endif
};
