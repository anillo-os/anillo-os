/*
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

#ifndef _FERRO_CORE_ENTRY_H_
#define _FERRO_CORE_ENTRY_H_

#include <stddef.h>

#include <ferro/base.h>
#include <ferro/platform.h>
#include <ferro/core/memory-regions.h>
#include <ferro/elf.h>
#include <ferro/core/framebuffer.h>
#include <ferro/core/acpi.h>

FERRO_DECLARATIONS_BEGIN;

typedef struct ferro_kernel_segment ferro_kernel_segment_t;
struct ferro_kernel_segment {
	// the number of bytes the segment occupies in memory
	size_t size;
	void* physical_address;
	void* virtual_address;
};

typedef struct ferro_kernel_image_info ferro_kernel_image_info_t;
struct ferro_kernel_image_info {
	void* physical_base_address;
	size_t size;
	size_t segment_count;
	ferro_kernel_segment_t* segments;
};

FERRO_ENUM(int, ferro_boot_data_type) {
	// default value; not a valid value
	ferro_boot_data_type_none,

	// pointer to where our ramdisk is stored
	ferro_boot_data_type_ramdisk,

	// pointer to where our config data (a.k.a. boot params) is stored
	ferro_boot_data_type_config,

	// pointer to where our kernel image information is stored
	ferro_boot_data_type_kernel_image_info,

	// pointer to where our kernel segment information table is stored
	ferro_boot_data_type_kernel_segment_info_table,

	// pointer to where our framebuffer information is stored
	ferro_boot_data_type_framebuffer_info,

	// pointer to where a pool of essential/permanent data is stored early in the boot process
	ferro_boot_data_type_initial_pool,

	// pointer to where our memory map is stored
	ferro_boot_data_type_memory_map,

	// pointer to the ACPI XSDT pointer (`facpi_rsdp_t`)
	ferro_boot_data_type_rsdp_pointer,
};

typedef struct ferro_boot_data_info ferro_boot_data_info_t;
struct ferro_boot_data_info {
	// what kind of boot data this entry is describing
	ferro_boot_data_type_t type;

	// physical start address of the data
	void* physical_address;

	// virtual start address of the data in the default kernel memory space
	void* virtual_address;

	// size in bytes of the data
	size_t size;
};

/**
 * Entry point for kernel core. Called by bootstraps.
 *
 * @param initial_pool            Pointer to beginning of initial pool of data for the kernel. May NOT be `NULL`.
 * @param initial_pool_page_count Size of the initial pool in 4KiB pages.
 * @param boot_data               Array of structures containing information about boot data the kernel needs/wants. May NOT be `NULL`. The table MUST contain at least two entries: one for the memory map and one for the kernel image information structure.
 * @param boot_data_count         Number of entires in the `boot_data_ array.
 *
 * @note The kernel assumes that all boot data passed into it except for the memory map is allocated within the initial pool.
 */
void ferro_entry(void* initial_pool, size_t initial_pool_page_count, ferro_boot_data_info_t* boot_data, size_t boot_data_count);

#if FERRO_ARCH == FERRO_ARCH_aarch64
	#define FERRO_SYSV_ABI
#elif FERRO_ARCH == FERRO_ARCH_x86_64
	#define FERRO_SYSV_ABI __attribute__((sysv_abi))
#else
	#error Unrecognized/unsupported CPU architecture! (See <ferro/core/entry.h>)
#endif

typedef FERRO_SYSV_ABI void (*ferro_entry_f)(void* initial_pool, size_t initial_pool_page_count, ferro_boot_data_info_t* boot_data, size_t boot_data_count);

// these are arch-dependent functions we expect all architectures to implement

/**
 * Hang the current CPU forever. Never returns.
 */
FERRO_ALWAYS_INLINE FERRO_NO_RETURN void fentry_hang_forever(void);

/**
 * Puts the current CPU to sleep until the next interrupt occurs.
 */
FERRO_ALWAYS_INLINE void fentry_idle(void);

/**
 * Permanently jump to a new (virtual) address. Never returns to the caller (at least not the same address).
 *
 * NOTE: This is *not* marked as no-return so that the compiler won't try to eliminate code after a call to it.
 *       This is because the function is used to jump into the kernel's higher-half after that has been setup,
 *       so it technically does return to the caller, just not at the original address.
 */
FERRO_ALWAYS_INLINE void fentry_jump_to_virtual(void* address);

FERRO_DECLARATIONS_END;

// now include the arch-dependent header
#if FERRO_ARCH == FERRO_ARCH_x86_64
	#include <ferro/core/x86_64/entry.h>
#elif FERRO_ARCH == FERRO_ARCH_aarch64
	#include <ferro/core/aarch64/entry.h>
#else
	#error Unrecognized/unsupported CPU architecture! (see <ferro/core/entry.h>)
#endif

#endif // _FERRO_CORE_ENTRY_H_
