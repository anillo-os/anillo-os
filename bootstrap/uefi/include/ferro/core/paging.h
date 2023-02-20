/*
 * This file is part of Anillo OS
 * Copyright (C) 2021 Anillo OS Developers
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

/**
 * @file
 *
 * Paging subsystem.
 */

#ifndef _FERRO_CORE_PAGING_H_
#define _FERRO_CORE_PAGING_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include <ferro/base.h>
#include <ferro/platform.h>
#include <ferro/error.h>
#include <ferro/core/memory-regions.h>
#include <ferro/bits.h>

FERRO_DECLARATIONS_BEGIN;

#define FERRO_KERNEL_VIRTUAL_START  ((uintptr_t)0xffff800000000000)

#define FERRO_KERNEL_IMAGE_BASE FERRO_KERNEL_VIRTUAL_START

/**
 * Used to translate addresses for static data (variables, functions, etc. compiled into the kernel image)
 * into physical address offsets relative to the kernel's base address (which can be different at every load).
 */
#define FERRO_KERNEL_STATIC_TO_OFFSET(x) (((uintptr_t)x - FERRO_KERNEL_IMAGE_BASE))

#define FERRO_PAGE_ALIGNED __attribute__((aligned(4096)))

#define FPAGE_PAGE_SIZE            0x00001000ULL
#define FPAGE_LARGE_PAGE_SIZE      0x00200000ULL
#define FPAGE_VERY_LARGE_PAGE_SIZE 0x40000000ULL
#define FPAGE_SUPER_LARGE_PAGE_SIZE 0x8000000000ULL

#define FPAGE_PAGE_ALIGNMENT 12

#define FPAGE_LARGE_PAGE_COUNT      (FPAGE_LARGE_PAGE_SIZE / FPAGE_PAGE_SIZE)
#define FPAGE_VERY_LARGE_PAGE_COUNT (FPAGE_VERY_LARGE_PAGE_SIZE / FPAGE_PAGE_SIZE)
#define FPAGE_SUPER_LARGE_PAGE_COUNT (FPAGE_SUPER_LARGE_PAGE_SIZE / FPAGE_PAGE_SIZE)

#define FPAGE_VIRT_L1_SHIFT 12
#define FPAGE_VIRT_L2_SHIFT 21
#define FPAGE_VIRT_L3_SHIFT 30
#define FPAGE_VIRT_L4_SHIFT 39

#define FPAGE_VIRT_OFFSET(x) ((uintptr_t)(x) & 0xfffULL)
#define FPAGE_VIRT_L1(x)     (((uintptr_t)(x) & (0x1ffULL << FPAGE_VIRT_L1_SHIFT)) >> FPAGE_VIRT_L1_SHIFT)
#define FPAGE_VIRT_L2(x)     (((uintptr_t)(x) & (0x1ffULL << FPAGE_VIRT_L2_SHIFT)) >> FPAGE_VIRT_L2_SHIFT)
#define FPAGE_VIRT_L3(x)     (((uintptr_t)(x) & (0x1ffULL << FPAGE_VIRT_L3_SHIFT)) >> FPAGE_VIRT_L3_SHIFT)
#define FPAGE_VIRT_L4(x)     (((uintptr_t)(x) & (0x1ffULL << FPAGE_VIRT_L4_SHIFT)) >> FPAGE_VIRT_L4_SHIFT)

#define FPAGE_VIRT_VERY_LARGE_OFFSET(x) ((uintptr_t)(x) & 0x000000003fffffffULL)
#define FPAGE_VIRT_LARGE_OFFSET(x)      ((uintptr_t)(x) & 0x00000000001fffffULL)

#define FPAGE_TABLE_ENTRY_MAX 511
#define FPAGE_TABLE_ENTRY_COUNT 512

FERRO_STRUCT(fpage_table) {
	uint64_t entries[FPAGE_TABLE_ENTRY_COUNT];
};

#define FPAGE_USER_MAX 0x7fffffffffff
#define FPAGE_USER_L4_MAX FPAGE_VIRT_L4(FPAGE_USER_MAX)

FERRO_ALWAYS_INLINE bool fpage_is_page_aligned(uintptr_t address) {
	return (address & (FPAGE_PAGE_SIZE - 1)) == 0;
};

FERRO_ALWAYS_INLINE bool fpage_is_large_page_aligned(uintptr_t address) {
	return (address & (FPAGE_LARGE_PAGE_SIZE - 1)) == 0;
};

FERRO_ALWAYS_INLINE bool fpage_is_very_large_page_aligned(uintptr_t address) {
	return (address & (FPAGE_VERY_LARGE_PAGE_SIZE - 1)) == 0;
};

FERRO_ALWAYS_INLINE bool fpage_is_super_large_page_aligned(uintptr_t address) {
	return (address & (FPAGE_SUPER_LARGE_PAGE_SIZE - 1)) == 0;
};

/**
 * Round a size (in bytes) up to a multiple of the current page size.
 */
FERRO_ALWAYS_INLINE uint64_t fpage_round_up_page(uint64_t number) {
	return (number + FPAGE_PAGE_SIZE - 1) & -FPAGE_PAGE_SIZE;
};

/**
 * Round a size (in bytes) down to a multiple of the current page size.
 */
FERRO_ALWAYS_INLINE uint64_t fpage_round_down_page(uint64_t number) {
	return number & -FPAGE_PAGE_SIZE;
};

FERRO_ALWAYS_INLINE uint64_t fpage_round_up_large_page(uint64_t number) {
	return (number + FPAGE_LARGE_PAGE_SIZE - 1) & -FPAGE_LARGE_PAGE_SIZE;
};

FERRO_ALWAYS_INLINE uint64_t fpage_round_down_large_page(uint64_t number) {
	return number & -FPAGE_LARGE_PAGE_SIZE;
};

FERRO_ALWAYS_INLINE uint64_t fpage_round_up_very_large_page(uint64_t number) {
	return (number + FPAGE_VERY_LARGE_PAGE_SIZE - 1) & -FPAGE_VERY_LARGE_PAGE_SIZE;
};

FERRO_ALWAYS_INLINE uint64_t fpage_round_down_very_large_page(uint64_t number) {
	return number & -FPAGE_VERY_LARGE_PAGE_SIZE;
};

FERRO_ALWAYS_INLINE uint64_t fpage_round_up_super_large_page(uint64_t number) {
	return (number + FPAGE_SUPER_LARGE_PAGE_SIZE - 1) & -FPAGE_SUPER_LARGE_PAGE_SIZE;
};

FERRO_ALWAYS_INLINE uint64_t fpage_round_down_super_large_page(uint64_t number) {
	return number & -FPAGE_SUPER_LARGE_PAGE_SIZE;
};

/**
 * Round the given number of bytes to a multiple of the page size, then return how many pages that is.
 *
 * e.g. If the input is 19 bytes, it'll round up to 4096 bytes, and then return 1 (because 4096 bytes is 1 page).
 */
FERRO_ALWAYS_INLINE uint64_t fpage_round_up_to_page_count(uint64_t byte_count) {
	return fpage_round_up_page(byte_count) / FPAGE_PAGE_SIZE;
};

FERRO_ALWAYS_INLINE uint64_t fpage_round_down_to_page_count(uint64_t byte_count) {
	return byte_count / FPAGE_PAGE_SIZE;
};

FERRO_ALWAYS_INLINE uint64_t fpage_round_up_to_large_page_count(uint64_t byte_count) {
	return fpage_round_up_large_page(byte_count) / FPAGE_LARGE_PAGE_SIZE;
};

FERRO_ALWAYS_INLINE uint64_t fpage_round_down_to_large_page_count(uint64_t byte_count) {
	return byte_count / FPAGE_LARGE_PAGE_SIZE;
};

FERRO_ALWAYS_INLINE uint64_t fpage_round_up_to_very_large_page_count(uint64_t byte_count) {
	return fpage_round_up_very_large_page(byte_count) / FPAGE_VERY_LARGE_PAGE_SIZE;
};

FERRO_ALWAYS_INLINE uint64_t fpage_round_down_to_very_large_page_count(uint64_t byte_count) {
	return byte_count / FPAGE_VERY_LARGE_PAGE_SIZE;
};

FERRO_ALWAYS_INLINE uint64_t fpage_round_up_to_super_large_page_count(uint64_t byte_count) {
	return fpage_round_up_super_large_page(byte_count) / FPAGE_SUPER_LARGE_PAGE_SIZE;
};

FERRO_ALWAYS_INLINE uint64_t fpage_round_down_to_super_large_page_count(uint64_t byte_count) {
	return byte_count / FPAGE_SUPER_LARGE_PAGE_SIZE;
};

/**
 * Returns the virtual address that contains the lookup information provided.
 */
FERRO_ALWAYS_INLINE uintptr_t fpage_make_virtual_address(size_t l4_index, size_t l3_index, size_t l2_index, size_t l1_index, uintptr_t offset) {
	uintptr_t result = 0;

	result |= (l4_index & 0x1ffULL) << FPAGE_VIRT_L4_SHIFT;
	result |= (l3_index & 0x1ffULL) << FPAGE_VIRT_L3_SHIFT;
	result |= (l2_index & 0x1ffULL) << FPAGE_VIRT_L2_SHIFT;
	result |= (l1_index & 0x1ffULL) << FPAGE_VIRT_L1_SHIFT;
	result |= offset & 0xfffULL;

	if (result & (1ULL << 47)) {
		result |= 0xffffULL << 48;
	}

	return result;
};

/**
 * Returns the address of the first boundary with the given alignment that the given region crosses.
 * If the region does not cross any boundaries with the given alignment, returns `0`.
 *
 * @note If the region starts on a boundary with the given alignment, that does not count as crossing it.
 *       Only boundaries *within* the region count as being crossed.
 *
 * @note A boundary alignment power greater than 63 is treated as having no boundary requirement and will always return `0`.
 */
FERRO_ALWAYS_INLINE uintptr_t fpage_region_boundary(uintptr_t start, size_t length, uint8_t boundary_alignment_power) {
	if (boundary_alignment_power > 63) {
		return 0;
	}
	uintptr_t boundary_alignment_mask = (1ull << boundary_alignment_power) - 1;
	uintptr_t next_boundary = (start & ~boundary_alignment_mask) + (1ull << boundary_alignment_power);
	return (next_boundary > start && next_boundary < start + length) ? next_boundary : 0;
};

FERRO_ALWAYS_INLINE uint8_t fpage_round_down_to_alignment_power(uint64_t byte_count) {
	if (byte_count == 0) {
		return 0;
	}
	return ferro_bits_in_use_u64(byte_count) - 1;
};

FERRO_ALWAYS_INLINE uint8_t fpage_round_up_to_alignment_power(uint64_t byte_count) {
	uint8_t power = fpage_round_down_to_alignment_power(byte_count);
	return ((1ull << power) < byte_count) ? (power + 1) : power;
};

FERRO_ALWAYS_INLINE uint64_t fpage_align_up(uint64_t byte_count) {
	if (byte_count == 0) {
		return 0;
	}
	return 1 << fpage_round_up_to_alignment_power(byte_count);
};

FERRO_ALWAYS_INLINE uint64_t fpage_align_down(uint64_t byte_count) {
	if (byte_count == 0) {
		return 0;
	}
	return 1 << fpage_round_down_to_alignment_power(byte_count);
};

FERRO_ALWAYS_INLINE uintptr_t fpage_align_address_down(uintptr_t address, uint8_t alignment_power) {
	return address & ~((1ull << alignment_power) - 1);
};

FERRO_ALWAYS_INLINE uintptr_t fpage_align_address_up(uintptr_t address, uint8_t alignment_power) {
	return (address + ((1ull << alignment_power) - 1)) & ~((1ull << alignment_power) - 1);
};

// these are arch-dependent functions we expect all architectures to implement

/**
 * Returns `true` if the given address is canonical (i.e. a valid format for the current platform), `false` otherwise.
 */
FERRO_ALWAYS_INLINE bool fpage_address_is_canonical(uintptr_t virtual_address);

FERRO_DECLARATIONS_END;

// now include the arch-dependent header
#if FERRO_ARCH == FERRO_ARCH_x86_64
	#include <ferro/core/x86_64/paging.h>
#elif FERRO_ARCH == FERRO_ARCH_aarch64
	#include <ferro/core/aarch64/paging.h>
#else
	#error Unrecognized/unsupported CPU architecture! (see <ferro/core/paging.h>)
#endif

#endif // _FERRO_CORE_PAGING_H_
