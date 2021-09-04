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
 * Memory region description definitions. Part of kernel entry information.
 */

#ifndef _FERRO_CORE_MEMORY_REGIONS_H_
#define _FERRO_CORE_MEMORY_REGIONS_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include <ferro/base.h>

FERRO_DECLARATIONS_BEGIN;

/**
 * @addtogroup Kernel-Entry
 *
 * @{
 */

FERRO_ENUM(int, ferro_memory_region_type) {
	/**
	 * Default value; not a valid value.
	 */
	ferro_memory_region_type_none,

	/**
	 * General multi-purpose memory.
	 */
	ferro_memory_region_type_general,

	/**
	 * General multi-purpose memory that also happens to be non-volatile.
	 */
	ferro_memory_region_type_nvram,

	/**
	 * Memory that is reserved for hardware use.
	 *
	 * Not to be arbitrarily touched by the OS, but some devices (e.g. framebuffers, interrupt controllers, etc.) might use this kind of memory for MMIO, in which case
	 * the OS may access/modify the memory **according to how the device dictates it must be used**.
	 */
	ferro_memory_region_type_hardware_reserved,

	/**
	 * Memory that is reserved until ACPI is enabled.
	 *
	 * Afterwards, it becomes general memory.
	 */
	ferro_memory_region_type_acpi_reclaim,

	/**
	 * Memory reserved for processor code.
	 *
	 * Never to be touched by the OS.
	 */
	ferro_memory_region_type_pal_code,

	/**
	 * Memory where special kernel data is stored on boot.
	 *
	 * This is usually permanent.
	 */
	ferro_memory_region_type_kernel_reserved,

	/**
	 * Memory where the kernel's entry stack is stored.
	 *
	 * This is reserved in early boot but can be turned into general memory later.
	 */
	ferro_memory_region_type_kernel_stack,
};

typedef struct ferro_memory_region ferro_memory_region_t;
struct ferro_memory_region {
	/**
	 * What kind of memory this memory region is.
	 */
	ferro_memory_region_type_t type;

	/**
	 * The physical start address of this memory region.
	 */
	uintptr_t physical_start;

	/**
	 * The virtual start address of this memory region.
	 */
	uintptr_t virtual_start;

	/**
	 * The number of 4KiB pages this memory region occupies.
	 */
	size_t page_count;
};

/**
 * @}
 */

FERRO_DECLARATIONS_END;

#endif /**
	 * _FERRO_CORE_MEMORY_REGIONS_H_
	 */
