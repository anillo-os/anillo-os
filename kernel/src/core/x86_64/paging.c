#include <ferro/core/x86_64/paging.h>

uintptr_t fpage_virtual_to_physical(uintptr_t virtual_address) {
	size_t l4_index = FPAGE_VIRT_L4(virtual_address);
	size_t l3_index = FPAGE_VIRT_L3(virtual_address);
	size_t l2_index = FPAGE_VIRT_L2(virtual_address);
	size_t l1_index = FPAGE_VIRT_L1(virtual_address);
	const fpage_table_t* l3 = (const fpage_table_t*)fpage_virtual_address_for_table(1, l4_index, 0, 0);
	const fpage_table_t* l2 = (const fpage_table_t*)fpage_virtual_address_for_table(2, l4_index, l3_index, 0);
	const fpage_table_t* l1 = (const fpage_table_t*)fpage_virtual_address_for_table(3, l4_index, l3_index, l2_index);
	uint64_t entry;

	entry = l3->entries[l3_index];
	if (entry & FPAGE_HUGE_BIT) {
		return FPAGE_PHYS_ENTRY(entry) | (virtual_address & FPAGE_VIRT_L3_HUGE_MASK);
	}

	entry = l2->entries[l2_index];
	if (entry & FPAGE_HUGE_BIT) {
		return FPAGE_PHYS_ENTRY(entry) | (virtual_address & FPAGE_VIRT_L2_HUGE_MASK);
	}

	return FPAGE_PHYS_ENTRY(l1->entries[l1_index]) | FPAGE_VIRT_OFFSET(virtual_address);
};
