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
//
// entry.c
//
// kernel entry point
//

#include <stdbool.h>
#include <stddef.h>
#include <ferro/core/entry.h>
#include <ferro/core/framebuffer.h>
#include <ferro/core/console.h>
#include <ferro/core/paging.h>

FERRO_INLINE void hang_forever() {
	while (true) {
		__asm__(
			"cli\n"
			"hlt\n"
		);
	}
};

static fpage_table_t page_table_level_1 FERRO_PAGE_ALIGNED = {0};
static fpage_table_t page_table_level_2 FERRO_PAGE_ALIGNED = {0};
static fpage_table_t page_table_level_2_identity FERRO_PAGE_ALIGNED = {0};
static fpage_table_t page_table_level_3 FERRO_PAGE_ALIGNED = {0};
static fpage_table_t page_table_level_3_identity FERRO_PAGE_ALIGNED = {0};
static fpage_table_t page_table_level_4 FERRO_PAGE_ALIGNED = {0};

// from https://stackoverflow.com/a/9194117/6620880
FERRO_INLINE uint64_t round_up_power_of_2(uint64_t number, uint64_t multiple) {
	return (number + multiple - 1) & -multiple;
};

FERRO_INLINE uint64_t round_down_power_of_2(uint64_t number, uint64_t multiple) {
	return number & -multiple;
};

FERRO_INLINE uint64_t round_up_div(uint64_t number, uint64_t multiple) {
	return (number + multiple - 1) / multiple;
};

// *must* be inlined because we can't make actual calls until this is done
FERRO_INLINE void setup_page_tables(uint16_t* next_l2, void* image_base, size_t image_size) {
	const void* phys_rbp = NULL;
	const void* phys_rsp = NULL;
	uintptr_t stack_diff = 0;
	const void* stack_page = NULL;

	// we have to access the physical addresses directly here
	fpage_table_t* const pt2          = (fpage_table_t*)(FERRO_KERNEL_VIRT_TO_PHYS(&page_table_level_2)          + image_base);
	fpage_table_t* const pt2_identity = (fpage_table_t*)(FERRO_KERNEL_VIRT_TO_PHYS(&page_table_level_2_identity) + image_base);
	fpage_table_t* const pt3          = (fpage_table_t*)(FERRO_KERNEL_VIRT_TO_PHYS(&page_table_level_3)          + image_base);
	fpage_table_t* const pt3_identity = (fpage_table_t*)(FERRO_KERNEL_VIRT_TO_PHYS(&page_table_level_3_identity) + image_base);
	fpage_table_t* const pt4          = (fpage_table_t*)(FERRO_KERNEL_VIRT_TO_PHYS(&page_table_level_4)          + image_base);
	size_t next_l2_idx = 0;

	// read the physical frame address
	__asm__("mov %%rbp, %0" : "=r" (phys_rbp));
	phys_rbp = (void*)fpage_virtual_to_physical((uintptr_t)phys_rbp);

	// set up 2MiB pages for the kernel image
	for (char* ptr = (char*)FERRO_KERNEL_VIRTUAL_START; (uintptr_t)ptr < FERRO_KERNEL_VIRTUAL_START + image_size; ptr += FPAGE_LARGE_PAGE_SIZE) {
		pt2->entries[next_l2_idx = FPAGE_VIRT_L2(ptr)] |= FPAGE_PRESENT_BIT | FPAGE_WRITABLE_BIT | FPAGE_HUGE_BIT | FPAGE_PHYS_ENTRY(FERRO_KERNEL_VIRT_TO_PHYS(ptr) + (uintptr_t)image_base);
	}
	++next_l2_idx; // assumes the kernel image will never occupy 1GiB

	// calculate the address of the 2MiB page containing the stack
	stack_page = (const void*)round_down_power_of_2((uintptr_t)phys_rbp, FPAGE_LARGE_PAGE_SIZE);

	// set up a 2MiB page for the stack
	pt2->entries[next_l2_idx] = FPAGE_PRESENT_BIT | FPAGE_WRITABLE_BIT | FPAGE_HUGE_BIT | FPAGE_PHYS_ENTRY((uintptr_t)stack_page);

	// calculate the virtual address of the current stack frame
	const void* virt_stack_bottom = (const void*)FPAGE_BUILD_VIRT(FPAGE_VIRT_L4(FERRO_KERNEL_VIRTUAL_START), FPAGE_VIRT_L3(FERRO_KERNEL_VIRTUAL_START), next_l2_idx, 0, 0);
	virt_stack_bottom = (const void*)((uintptr_t)virt_stack_bottom + (phys_rbp - stack_page));
	++next_l2_idx;

	// temporarily identity map the kernel image so the RIP doesn't fail
	for (char* ptr = image_base; ptr < (char*)image_base + image_size; ptr += FPAGE_LARGE_PAGE_SIZE) {
		pt2_identity->entries[FPAGE_VIRT_L2(ptr)] |= FPAGE_PRESENT_BIT | FPAGE_WRITABLE_BIT | FPAGE_HUGE_BIT | FPAGE_PHYS_ENTRY(ptr);
	}

	         pt4->entries[FPAGE_VIRT_L4(FERRO_KERNEL_VIRTUAL_START)] |= FPAGE_PRESENT_BIT | FPAGE_WRITABLE_BIT | FPAGE_PHYS_ENTRY(pt3);
	         pt4->entries[                FPAGE_VIRT_L4(image_base)] |= FPAGE_PRESENT_BIT | FPAGE_WRITABLE_BIT | FPAGE_PHYS_ENTRY(pt3_identity);
	         pt3->entries[FPAGE_VIRT_L3(FERRO_KERNEL_VIRTUAL_START)] |= FPAGE_PRESENT_BIT | FPAGE_WRITABLE_BIT | FPAGE_PHYS_ENTRY(pt2);
	pt3_identity->entries[                FPAGE_VIRT_L3(image_base)] |= FPAGE_PRESENT_BIT | FPAGE_WRITABLE_BIT | FPAGE_PHYS_ENTRY(pt2_identity);

	// read the current physical stack top address
	__asm__("mov %%rsp, %0\n" : "=r" (phys_rsp));
	phys_rsp = (void*)fpage_virtual_to_physical((uintptr_t)phys_rsp);
	stack_diff = (uintptr_t)phys_rbp - (uintptr_t)phys_rsp;

	// set the outgoing value for the next l2 index
	*next_l2 = next_l2_idx;

	// overwrite the page table
	__asm__(
		"mov %0, %%cr3\n"
		"mov %1, %%rbp\n"
		"mov %2, %%rsp\n"
		::
		"r" (pt4),
		"r" (virt_stack_bottom),
		"r" ((uintptr_t)virt_stack_bottom - stack_diff)
	);
};

// maps the regions that the kernel needs early on
//
// NOTE!! this function assumes all boot data is allocated in the initial pool (except for the memory map)
static void map_regions(uint16_t* next_l2, ferro_memory_region_t** memory_regions_ptr, size_t memory_region_count, void** initial_pool_ptr, size_t initial_pool_page_count, ferro_boot_data_info_t** boot_data_ptr, size_t boot_data_count, void* image_base, size_t image_size) {
	uintptr_t initial_pool_phys_start = 0;
	uintptr_t initial_pool_phys_end = 0;
	uintptr_t initial_pool_virt_start = 0;
	void* kernel_segment_info_table = NULL;
	uint16_t next_l1_idx = 0;
	size_t memory_regions_array_size = memory_region_count * sizeof(ferro_memory_region_t);
	uint16_t l2_idx = (*next_l2)++;

	page_table_level_2.entries[l2_idx] = FPAGE_PRESENT_BIT | FPAGE_WRITABLE_BIT | FPAGE_PHYS_ENTRY(FERRO_KERNEL_VIRT_TO_PHYS(&page_table_level_1) + image_base);

	// first, map the memory region array itself
	// it's guaranteed to be allocated on a page boundary
	ferro_memory_region_t* physical_memory_regions_address = *memory_regions_ptr;
	ferro_memory_region_t* new_memory_regions_address = (ferro_memory_region_t*)FPAGE_BUILD_VIRT(FPAGE_VIRT_L4(FERRO_KERNEL_VIRTUAL_START), FPAGE_VIRT_L3(FERRO_KERNEL_VIRTUAL_START), l2_idx, next_l1_idx, 0);
	for (size_t i = 0; i < memory_regions_array_size; i += FPAGE_PAGE_SIZE) {
		page_table_level_1.entries[next_l1_idx++] = FPAGE_PRESENT_BIT | FPAGE_WRITABLE_BIT | FPAGE_PHYS_ENTRY((uintptr_t)physical_memory_regions_address + i);
	}
	*memory_regions_ptr = new_memory_regions_address;

	// loop through the memory regions and map the regions we need right now
	for (size_t i = 0; i < memory_region_count; ++i) {
		ferro_memory_region_t* region = &new_memory_regions_address[i];
		// if it's not a kernel reserved section, we don't care right now
		if (region->type != ferro_memory_region_type_kernel_reserved) {
			continue;
		}
		// map it if it's not already mapped
		if (region->virtual_start == 0) {
			// we've already mapped the memory regions array at the start of this function
			if (region->physical_start == (uintptr_t)physical_memory_regions_address) {
				region->virtual_start = (uintptr_t)new_memory_regions_address;
				continue;
			}
			// we can only allocate 2MiB pages if the address is on a 2MiB page boundary
			if ((region->physical_start & FPAGE_LARGE_PAGE_SIZE) == 0 && region->page_count > (512 - next_l1_idx)) {
				// allocate it in 2MiB pages
				for (size_t j = 0; j < (region->page_count + 511) / 512; ++j) {
					if (j == 0) {
						region->virtual_start = FPAGE_BUILD_VIRT(FPAGE_VIRT_L4(FERRO_KERNEL_VIRTUAL_START), FPAGE_VIRT_L3(FERRO_KERNEL_VIRTUAL_START), *next_l2, 0, 0);
					}
					page_table_level_2.entries[(*next_l2)++] = FPAGE_PRESENT_BIT | FPAGE_WRITABLE_BIT | FPAGE_HUGE_BIT | round_down_power_of_2(FPAGE_PHYS_ENTRY((uintptr_t)region->physical_start + (j * FPAGE_LARGE_PAGE_SIZE)), FPAGE_LARGE_PAGE_SIZE);
				}
			} else {
				// allocate it in 4KiB
				for (size_t j = 0; j < region->page_count; ++j) {
					if (j == 0) {
						region->virtual_start = FPAGE_BUILD_VIRT(FPAGE_VIRT_L4(FERRO_KERNEL_VIRTUAL_START), FPAGE_VIRT_L3(FERRO_KERNEL_VIRTUAL_START), l2_idx, next_l1_idx, 0);
					}
					page_table_level_1.entries[next_l1_idx++] = FPAGE_PRESENT_BIT | FPAGE_WRITABLE_BIT | round_down_power_of_2(FPAGE_PHYS_ENTRY((uintptr_t)region->physical_start + (j * FPAGE_PAGE_SIZE)), FPAGE_PAGE_SIZE);
				}
			}
		}
		if (region->physical_start == (uintptr_t)*initial_pool_ptr) {
			*initial_pool_ptr = (void*)region->virtual_start;
			*boot_data_ptr = (void*)(region->virtual_start + ((uintptr_t)*boot_data_ptr - region->physical_start));

			for (size_t j = 0; j < boot_data_count; ++j) {
				ferro_boot_data_info_t* data = &(*boot_data_ptr)[j];
				if (data->type == ferro_boot_data_type_memory_map) {
					data->virtual_address = new_memory_regions_address;
				} else {
					data->virtual_address = (void*)(region->virtual_start + ((uintptr_t)data->physical_address - region->physical_start));
					if (data->type == ferro_boot_data_type_kernel_image_info) {
						ferro_kernel_image_info_t* info = data->virtual_address;
						info->segments = (void*)(region->virtual_start + ((uintptr_t)info->segments - region->physical_start));
					}
				}
			}
		}
	}

	// map the framebuffer (if we have one)
	for (size_t i = 0; i < boot_data_count; ++i) {
		ferro_boot_data_info_t* data = &(*boot_data_ptr)[i];
		if (data->type == ferro_boot_data_type_framebuffer_info) {
			ferro_fb_info_t* fb_info = data->virtual_address;
			size_t fb_page_count = round_up_div(fb_info->scan_line_size * fb_info->height, FPAGE_PAGE_SIZE);
			void* fb_phys = fb_info->base;
			// we can only allocate 2MiB pages if the address is on a 2MiB page boundary
			if (((uintptr_t)fb_phys & FPAGE_LARGE_PAGE_SIZE) == 0 && fb_page_count > (512 - next_l1_idx)) {
				// allocate it in 2MiB pages
				for (size_t j = 0; j < (fb_page_count + 511) / 512; ++j) {
					if (j == 0) {
						fb_info->base = (void*)FPAGE_BUILD_VIRT(FPAGE_VIRT_L4(FERRO_KERNEL_VIRTUAL_START), FPAGE_VIRT_L3(FERRO_KERNEL_VIRTUAL_START), *next_l2, 0, 0);
					}
					page_table_level_2.entries[(*next_l2)++] = FPAGE_PRESENT_BIT | FPAGE_WRITABLE_BIT | FPAGE_HUGE_BIT | round_down_power_of_2(FPAGE_PHYS_ENTRY((uintptr_t)fb_phys + (j * FPAGE_LARGE_PAGE_SIZE)), FPAGE_LARGE_PAGE_SIZE);
				}
			} else {
				// allocate it in 4KiB
				for (size_t j = 0; j < fb_page_count; ++j) {
					if (j == 0) {
						fb_info->base = (void*)FPAGE_BUILD_VIRT(FPAGE_VIRT_L4(FERRO_KERNEL_VIRTUAL_START), FPAGE_VIRT_L3(FERRO_KERNEL_VIRTUAL_START), l2_idx, next_l1_idx, 0);
					}
					page_table_level_1.entries[next_l1_idx++] = FPAGE_PRESENT_BIT | FPAGE_WRITABLE_BIT | round_down_power_of_2(FPAGE_PHYS_ENTRY((uintptr_t)fb_phys + (j * FPAGE_PAGE_SIZE)), FPAGE_PAGE_SIZE);
				}
			}
		}
	}
};

__attribute__((section(".text.ferro_entry")))
void ferro_entry(void* initial_pool, size_t initial_pool_page_count, ferro_boot_data_info_t* boot_data, size_t boot_data_count) {
	uint16_t next_l2 = 0;
	ferro_memory_region_t* memory_map = NULL;
	size_t memory_map_length = 0;
	ferro_fb_info_t* fb_info = NULL;
	ferro_kernel_image_info_t* image_info = NULL;
	void* image_base = NULL;
	size_t image_size = 0;

	for (size_t i = 0; i < boot_data_count; ++i) {
		ferro_boot_data_info_t* curr = &boot_data[i];
		if (curr->type == ferro_boot_data_type_memory_map) {
			memory_map = curr->physical_address;
			memory_map_length = curr->size / sizeof(ferro_memory_region_t);
		} else if (curr->type == ferro_boot_data_type_kernel_image_info) {
			image_info = curr->physical_address;
			image_base = image_info->physical_base_address;
			image_size = image_info->size;
		}
	}

	// ALWAYS DO THIS BEFORE ANY ACTUAL FUNCTION CALLS
	setup_page_tables(&next_l2, image_base, image_size);

	// finally, fully switch to the higher-half by jumping into the new virtual RIP
	__asm__("jmp *%0" :: "r" (&&jump_here_for_virtual));
jump_here_for_virtual:;

	map_regions(&next_l2, &memory_map, memory_map_length, &initial_pool, initial_pool_page_count, &boot_data, boot_data_count, image_base, image_size);

	for (size_t i = 0; i < boot_data_count; ++i) {
		ferro_boot_data_info_t* curr = &boot_data[i];
		if (curr->type == ferro_boot_data_type_framebuffer_info) {
			fb_info = curr->virtual_address;
		}
	}

	ferro_fb_init(fb_info);
	fconsole_init();

	hang_forever();
};
