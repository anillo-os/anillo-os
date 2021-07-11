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

#ifndef _FERRO_CORE_ENTRY_H_
#define _FERRO_CORE_ENTRY_H_

#include <stddef.h>

#include <ferro/base.h>
#include <ferro/core/memory-regions.h>
#include <ferro/elf.h>
#include <ferro/core/framebuffer.h>

FERRO_DECLARATIONS_BEGIN;

typedef struct ferro_kernel_segment ferro_kernel_segment_t;
struct ferro_kernel_segment {
	size_t page_count;
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

typedef __attribute__((sysv_abi)) void (*ferro_entry_t)(void* initial_pool, size_t initial_pool_page_count, ferro_boot_data_info_t* boot_data, size_t boot_data_count);

FERRO_DECLARATIONS_END;

#endif // _FERRO_CORE_ENTRY_H_
