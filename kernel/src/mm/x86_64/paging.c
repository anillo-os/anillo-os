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

#include <ferro/core/x86_64/paging.private.h>
#include <ferro/core/panic.h>
#include <ferro/kasan.h>

uintptr_t fpage_virtual_to_physical(uintptr_t virtual_address) {
	if (!fpage_address_is_canonical(virtual_address)) {
		return UINTPTR_MAX;
	}

	size_t l4_index = FPAGE_VIRT_L4(virtual_address);
	size_t l3_index = FPAGE_VIRT_L3(virtual_address);
	size_t l2_index = FPAGE_VIRT_L2(virtual_address);
	size_t l1_index = FPAGE_VIRT_L1(virtual_address);
	uint64_t entry;

	entry = fpage_table_load(1, l4_index, 0, 0, 0);
	if (!fpage_entry_is_active(entry)) {
		return UINTPTR_MAX;
	}

	entry = fpage_table_load(2, l4_index, l3_index, 0, 0);
	if (!fpage_entry_is_active(entry)) {
		return UINTPTR_MAX;
	}
	if (entry & FARCH_PAGE_HUGE_BIT) {
		return FARCH_PAGE_PHYS_ENTRY(entry) | (virtual_address & FARCH_PAGE_VIRT_L3_HUGE_MASK);
	}

	entry = fpage_table_load(3, l4_index, l3_index, l2_index, 0);
	if (!fpage_entry_is_active(entry)) {
		return UINTPTR_MAX;
	}
	if (entry & FARCH_PAGE_HUGE_BIT) {
		return FARCH_PAGE_PHYS_ENTRY(entry) | (virtual_address & FARCH_PAGE_VIRT_L2_HUGE_MASK);
	}

	entry = fpage_table_load(4, l4_index, l3_index, l2_index, l1_index);
	if (!fpage_entry_is_active(entry)) {
		return UINTPTR_MAX;
	}

	return FARCH_PAGE_PHYS_ENTRY(entry) | FPAGE_VIRT_OFFSET(virtual_address);
};

static void invalidate_tlb_work(void* address) {
	__asm__ volatile("invlpg %0" :: "m" (address) : "memory");
};

void farch_page_invalidate_tlb_for_address_other_cpus(void* address) {
	fpanic_status(fcpu_interrupt_all(invalidate_tlb_work, address, false, true));
};

static void invalidate_tlb_full_work(void* ignored) {
	uint64_t addr;
	__asm__ volatile("mov %%cr3, %0" : "=r" (addr));
	__asm__ volatile("mov %0, %%cr3\n" :: "r" (addr) : "memory");
};

void farch_page_invalidate_tlb_full_other_cpus(void) {
	fpanic_status(fcpu_interrupt_all(invalidate_tlb_full_work, NULL, false, true));
};
