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
 * Paging subsystem; private components.
 */

#ifndef _FERRO_CORE_PAGING_PRIVATE_H_
#define _FERRO_CORE_PAGING_PRIVATE_H_

#include <ferro/core/paging.h>

FERRO_DECLARATIONS_BEGIN;

// these are arch-dependent functions we expect all architectures to implement

/**
 * Creates a 4KiB page table entry with the given information.
 */
FERRO_ALWAYS_INLINE uint64_t fpage_page_entry(uintptr_t physical_address, bool writable);

/**
 * Creates a 2MiB page table entry with the given information.
 */
FERRO_ALWAYS_INLINE uint64_t fpage_large_page_entry(uintptr_t physical_address, bool writable);

/**
 * Creates a 1GiB page table entry with the given information.
 */
FERRO_ALWAYS_INLINE uint64_t fpage_very_large_page_entry(uintptr_t physical_address, bool writable);

/**
 * Creates a page table entry to point to another page table.
 */
FERRO_ALWAYS_INLINE uint64_t fpage_table_entry(uintptr_t physical_address, bool writable);

/**
 * Jumps into a new virtual memory mapping using the given base table address and stack address.
 */
FERRO_ALWAYS_INLINE void fpage_begin_new_mapping(void* l4_address, void* old_stack_bottom, void* new_stack_bottom);

/**
 * Translates the given virtual address into a physical address. Only valid during early startup.
 */
FERRO_ALWAYS_INLINE uintptr_t fpage_virtual_to_physical_early(uintptr_t virtual_address);

/**
 * Determines whether an entry with the given value is active or not.
 */
FERRO_ALWAYS_INLINE bool fpage_entry_is_active(uint64_t entry_value);

/**
 * On architectures where this is necessary, triggers a synchronization. This is meant to be called after any table modification.
 */
FERRO_ALWAYS_INLINE void fpage_synchronize_after_table_modification(void);

/**
 * Returns `true` if the given entry represents a large or very large page.
 */
FERRO_ALWAYS_INLINE bool fpage_entry_is_large_page_entry(uint64_t entry);

/**
 * Creates a modified page table entry from the given entry, disabling caching for that page.
 */
FERRO_ALWAYS_INLINE uint64_t fpage_entry_disable_caching(uint64_t entry);

/**
 * Returns the address associated with the given entry.
 */
FERRO_ALWAYS_INLINE uintptr_t fpage_entry_address(uint64_t entry);

/**
 * Creates a modified entry from the given entry, marking it either as active or inactive (depending on @p active).
 */
FERRO_ALWAYS_INLINE uint64_t fpage_entry_mark_active(uint64_t entry, bool active);

/**
 * Creates a modified entry from the given entry, marking it either as privileged or unprivileged (depending on @p privileged).
 */
FERRO_ALWAYS_INLINE uint64_t fpage_entry_mark_privileged(uint64_t entry, bool privileged);

// now include the arch-dependent header
#if FERRO_ARCH == FERRO_ARCH_x86_64
	#include <ferro/core/x86_64/paging.private.h>
#elif FERRO_ARCH == FERRO_ARCH_aarch64
	#include <ferro/core/aarch64/paging.private.h>
#else
	#error Unrecognized/unsupported CPU architecture! (see <ferro/core/paging.private.h>)
#endif

FERRO_DECLARATIONS_END;

#endif // _FERRO_CORE_PAGING_PRIVATE_H_
