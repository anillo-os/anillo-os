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
 * x86_64-specific paging function implementations.
 */

#include <ferro/core/x86_64/paging.h>

uintptr_t fpage_virtual_to_physical(uintptr_t virtual_address) {
	size_t l4_index = FPAGE_VIRT_L4(virtual_address);
	size_t l3_index = FPAGE_VIRT_L3(virtual_address);
	size_t l2_index = FPAGE_VIRT_L2(virtual_address);
	size_t l1_index = FPAGE_VIRT_L1(virtual_address);
	const fpage_table_t* l4 = (const fpage_table_t*)fpage_virtual_address_for_table(0, 0, 0, 0);
	const fpage_table_t* l3 = (const fpage_table_t*)fpage_virtual_address_for_table(1, l4_index, 0, 0);
	const fpage_table_t* l2 = (const fpage_table_t*)fpage_virtual_address_for_table(2, l4_index, l3_index, 0);
	const fpage_table_t* l1 = (const fpage_table_t*)fpage_virtual_address_for_table(3, l4_index, l3_index, l2_index);
	uint64_t entry;

	entry = l4->entries[l4_index];
	if (!fpage_entry_is_active(entry)) {
		return UINTPTR_MAX;
	}

	entry = l3->entries[l3_index];
	if (!fpage_entry_is_active(entry)) {
		return UINTPTR_MAX;
	}
	if (entry & FARCH_PAGE_HUGE_BIT) {
		return FARCH_PAGE_PHYS_ENTRY(entry) | (virtual_address & FARCH_PAGE_VIRT_L3_HUGE_MASK);
	}

	entry = l2->entries[l2_index];
	if (!fpage_entry_is_active(entry)) {
		return UINTPTR_MAX;
	}
	if (entry & FARCH_PAGE_HUGE_BIT) {
		return FARCH_PAGE_PHYS_ENTRY(entry) | (virtual_address & FARCH_PAGE_VIRT_L2_HUGE_MASK);
	}

	entry = l1->entries[l1_index];
	if (!fpage_entry_is_active(entry)) {
		return UINTPTR_MAX;
	}

	return FARCH_PAGE_PHYS_ENTRY(entry) | FPAGE_VIRT_OFFSET(virtual_address);
};
