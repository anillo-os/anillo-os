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
 * Common kernel entry point.
 */

//
// interestingly, for the two architectures that we currently support (x86_64 and AARCH64),
// we can actually share a majority of the startup code between them. this is possible thanks to
// architectural similarities between the two, especially in areas like paging.
//

#include <stdbool.h>
#include <stddef.h>
#include <ferro/core/entry.h>
#include <ferro/core/framebuffer.h>
#include <ferro/core/console.h>
#include <ferro/core/paging.h>
#include <ferro/core/panic.h>
#include <ferro/core/interrupts.h>
#include <ferro/core/acpi.h>
#include <ferro/core/timers.h>
#include <ferro/core/scheduler.h>
#include <ferro/core/workers.h>
#include <ferro/core/serial.h>
#include <ferro/core/config.h>
#include <ferro/gdbstub/gdbstub.h>
#include <ferro/core/vfs.h>
#include <ferro/core/ramdisk.h>

#include <libk/libk.h>

#if FERRO_ARCH == FERRO_ARCH_x86_64
	#include <ferro/core/x86_64/apic.h>
	#include <ferro/core/x86_64/tsc.h>
#elif FERRO_ARCH == FERRO_ARCH_aarch64
	#include <ferro/core/aarch64/gic.h>
	#include <ferro/core/aarch64/generic-timer.h>
#endif

#include <ferro/core/mempool.h>

static fpage_table_t page_table_level_1          FERRO_PAGE_ALIGNED = {0};
static fpage_table_t page_table_level_2          FERRO_PAGE_ALIGNED = {0};
static fpage_table_t page_table_level_2_identity FERRO_PAGE_ALIGNED = {0};
static fpage_table_t page_table_level_3          FERRO_PAGE_ALIGNED = {0};
static fpage_table_t page_table_level_3_identity FERRO_PAGE_ALIGNED = {0};
static fpage_table_t page_table_level_4          FERRO_PAGE_ALIGNED = {0};

// from https://stackoverflow.com/a/9194117/6620880
FERRO_ALWAYS_INLINE uint64_t round_up_power_of_2(uint64_t number, uint64_t multiple) {
	return (number + multiple - 1) & -multiple;
};

FERRO_ALWAYS_INLINE uint64_t round_down_power_of_2(uint64_t number, uint64_t multiple) {
	return number & -multiple;
};

FERRO_ALWAYS_INLINE uint64_t round_up_div(uint64_t number, uint64_t multiple) {
	return (number + multiple - 1) / multiple;
};

// *must* be inlined because we can't make actual calls until this is done
FERRO_ALWAYS_INLINE void setup_page_tables(uint16_t* next_l2, void* image_base, size_t image_size) {
	void* phys_frame_pointer = NULL;
	uintptr_t stack_diff = 0;
	void* stack_page = NULL;
	void* virt_stack_bottom = NULL;

	// we have to access the physical addresses directly here
	fpage_table_t* const pt2          = (fpage_table_t*)(FERRO_KERNEL_VIRT_TO_PHYS(&page_table_level_2)          + image_base);
	fpage_table_t* const pt2_identity = (fpage_table_t*)(FERRO_KERNEL_VIRT_TO_PHYS(&page_table_level_2_identity) + image_base);
	fpage_table_t* const pt3          = (fpage_table_t*)(FERRO_KERNEL_VIRT_TO_PHYS(&page_table_level_3)          + image_base);
	fpage_table_t* const pt3_identity = (fpage_table_t*)(FERRO_KERNEL_VIRT_TO_PHYS(&page_table_level_3_identity) + image_base);
	fpage_table_t* const pt4          = (fpage_table_t*)(FERRO_KERNEL_VIRT_TO_PHYS(&page_table_level_4)          + image_base);
	size_t next_l2_idx = 0;

	// read the physical frame address
	phys_frame_pointer = __builtin_frame_address(0);
	phys_frame_pointer = (void*)fpage_virtual_to_physical_early((uintptr_t)phys_frame_pointer);

	// set up 2MiB pages for the kernel image
	for (char* ptr = (char*)FERRO_KERNEL_VIRTUAL_START; (uintptr_t)ptr < FERRO_KERNEL_VIRTUAL_START + image_size; ptr += FPAGE_LARGE_PAGE_SIZE) {
		pt2->entries[next_l2_idx = FPAGE_VIRT_L2(ptr)] = fpage_large_page_entry(FERRO_KERNEL_VIRT_TO_PHYS(ptr) + (uintptr_t)image_base, true);
	}
	++next_l2_idx; // assumes the kernel image will never occupy 1GiB

	// calculate the address of the 2MiB page containing the stack
	stack_page = (void*)round_down_power_of_2((uintptr_t)phys_frame_pointer, FPAGE_LARGE_PAGE_SIZE);

	// set up a 2MiB page for the stack
	pt2->entries[next_l2_idx] = fpage_large_page_entry((uintptr_t)stack_page, true);

	// calculate the virtual address of the current stack frame
	virt_stack_bottom = (void*)fpage_make_virtual_address(FPAGE_VIRT_L4(FERRO_KERNEL_VIRTUAL_START), FPAGE_VIRT_L3(FERRO_KERNEL_VIRTUAL_START), next_l2_idx, 0, 0);
	virt_stack_bottom = (void*)((uintptr_t)virt_stack_bottom + (phys_frame_pointer - stack_page));
	++next_l2_idx;

	// temporarily identity map the kernel image so the RIP doesn't fail
	for (char* ptr = image_base; ptr < (char*)image_base + image_size; ptr += FPAGE_LARGE_PAGE_SIZE) {
		pt2_identity->entries[FPAGE_VIRT_L2(ptr)] = fpage_large_page_entry((uintptr_t)ptr, true);
	}

	         pt4->entries[FPAGE_VIRT_L4(FERRO_KERNEL_VIRTUAL_START)] = fpage_table_entry((uintptr_t)pt3, true);
	         pt4->entries[                FPAGE_VIRT_L4(image_base)] = fpage_table_entry((uintptr_t)pt3_identity, true);
	         pt3->entries[FPAGE_VIRT_L3(FERRO_KERNEL_VIRTUAL_START)] = fpage_table_entry((uintptr_t)pt2, true);
	pt3_identity->entries[                FPAGE_VIRT_L3(image_base)] = fpage_table_entry((uintptr_t)pt2_identity, true);

	// set the outgoing value for the next l2 index
	*next_l2 = next_l2_idx;

	// start the new mapping
	fpage_begin_new_mapping(pt4, phys_frame_pointer, virt_stack_bottom);
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

	page_table_level_2.entries[l2_idx] = fpage_table_entry(FERRO_KERNEL_VIRT_TO_PHYS(&page_table_level_1) + (uintptr_t)image_base, true);

	// first, map the memory region array itself
	// it's guaranteed to be allocated on a page boundary
	ferro_memory_region_t* physical_memory_regions_address = *memory_regions_ptr;
	ferro_memory_region_t* new_memory_regions_address = (ferro_memory_region_t*)fpage_make_virtual_address(FPAGE_VIRT_L4(FERRO_KERNEL_VIRTUAL_START), FPAGE_VIRT_L3(FERRO_KERNEL_VIRTUAL_START), l2_idx, next_l1_idx, 0);
	for (size_t i = 0; i < memory_regions_array_size; i += FPAGE_PAGE_SIZE) {
		page_table_level_1.entries[next_l1_idx++] = fpage_page_entry((uintptr_t)physical_memory_regions_address + i, true);
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
			if (fpage_is_large_page_aligned(region->physical_start) && region->page_count > (512 - next_l1_idx)) {
				// allocate it in 2MiB pages
				for (size_t j = 0; j < (region->page_count + 511) / 512; ++j) {
					if (j == 0) {
						region->virtual_start = fpage_make_virtual_address(FPAGE_VIRT_L4(FERRO_KERNEL_VIRTUAL_START), FPAGE_VIRT_L3(FERRO_KERNEL_VIRTUAL_START), *next_l2, 0, 0);
					}
					page_table_level_2.entries[(*next_l2)++] = fpage_large_page_entry((uintptr_t)region->physical_start + (j * FPAGE_LARGE_PAGE_SIZE), true);
				}
			} else {
				// allocate it in 4KiB
				for (size_t j = 0; j < region->page_count; ++j) {
					if (j == 0) {
						region->virtual_start = fpage_make_virtual_address(FPAGE_VIRT_L4(FERRO_KERNEL_VIRTUAL_START), FPAGE_VIRT_L3(FERRO_KERNEL_VIRTUAL_START), l2_idx, next_l1_idx, 0);
					}
					page_table_level_1.entries[next_l1_idx++] = fpage_page_entry((uintptr_t)region->physical_start + (j * FPAGE_PAGE_SIZE), true);
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
};

#if 0
static void do_work1(void* data) {
	for (size_t i = 0; i < 5; ++i) {
		fconsole_logf("Doing some work in worker 1: %zu\n", i);
		FERRO_WUR_IGNORE(fthread_suspend_timeout(NULL, 1000000000ULL, fthread_timeout_type_ns_relative));
	}
};

static void do_work2(void* data) {
	for (size_t i = 0; i < 10; ++i) {
		fconsole_logf("Doing some work in worker 2: %zu\n", i);
		FERRO_WUR_IGNORE(fthread_suspend_timeout(NULL, 1000000000ULL / 2, fthread_timeout_type_ns_relative));
	}
};

static void do_work3(void* data) {
	fconsole_log("The third worker was called!\n");
};
#endif

static ferro_ramdisk_t* ramdisk = NULL;

static void ferro_entry_threaded(void* data) {
	fconsole_log("Entering threaded kernel startup\n");

	fworkers_init();

	fvfs_init();

	if (ramdisk) {
		ferro_ramdisk_init(ramdisk);
	} else {
		fpanic("No ramdisk found!");
	}

#if TEST_WORKERS
	fwork_t* worker1;
	fwork_t* worker2;

	if (fwork_new(do_work1, NULL, &worker1) != ferr_ok) {
		fpanic("Failed to create first worker");
	}

	if (fwork_new(do_work2, NULL, &worker2) != ferr_ok) {
		fpanic("Failed to create second worker");
	}

	if (fwork_schedule(worker1, 0) != ferr_ok) {
		fpanic("Failed to schedule first worker");
	}

	if (fwork_schedule(worker2, 0) != ferr_ok) {
		fpanic("Failed to schedule second worker");
	}

	fwork_wait(worker1);
	fwork_wait(worker2);

	fwork_release(worker1);
	fwork_release(worker2);

	fconsole_log("Both work items have completed, now we'll schedule a third to run in 3 seconds.\n");

	if (fwork_schedule_new(do_work3, NULL, 1000000000ULL * 3, NULL) != ferr_ok) {
		fpanic("Failed to schedule third worker");
	}
#endif

#if TEST_VFS
	fvfs_descriptor_t* desc;

	fpanic_status(fvfs_open("/test1.txt", fvfs_descriptor_flag_read, &desc));
	fconsole_log("Successfully opened descriptor for \"/test.txt\"!\n");

	#define BUFFER_SIZE 10
	#define ABSOLUTE_PATHS false
	char path[BUFFER_SIZE];
	size_t path_len = 0;
	switch (fvfs_copy_path(desc, ABSOLUTE_PATHS, path, sizeof(path), &path_len)) {
		case ferr_ok: {
			fconsole_logf("Path is \"%.*s\"\n", (int)path_len, path);
		} break;
		case ferr_too_big: {
			fconsole_log("Path was too big for buffer\n");
		} break;
		default: {
			fpanic("fvfs_copy_path failed for unexpected reason");
		} break;
	}

	fvfs_release(desc);

	#define ARRAY_LENGTH 1

	fvfs_path_t children[ARRAY_LENGTH];
	size_t count = 0;
	fvfs_list_children_context_t context;
	fpanic_status(fvfs_open("/", fvfs_descriptor_flag_read, &desc));
	fconsole_log("Succesfully opened descriptor for \"/\"!\n");

	for (ferr_t status = fvfs_list_children_init(desc, children, sizeof(children) / sizeof(*children), ABSOLUTE_PATHS, &count, &context); status == ferr_ok; status = fvfs_list_children(desc, children, sizeof(children) / sizeof(*children), ABSOLUTE_PATHS, &count, &context)) {
		fconsole_logf("Found %zu child%s\n", count, count == 1 ? "" : "ren");

		for (size_t i = 0; i < count; ++i) {
			fvfs_descriptor_t* child;
			fvfs_node_info_t info;

			fpanic_status(fvfs_open_rn(desc, children[i].path, children[i].length, fvfs_descriptor_flag_read, &child));

			fpanic_status(fvfs_copy_info(child, &info));

			fconsole_logf("Opened descriptor for child: %.*s (which is %s)\n", (int)children[i].length, children[i].path, info.type == fvfs_node_type_directory ? "a directory" : (info.type == fvfs_node_type_file ? "a file" : "unknown"));

			fvfs_release(child);
		}
	}

	fconsole_log("Finished iterating children\n");

	fpanic_status(fvfs_list_children_finish(desc, children, count, &context));

	fvfs_release(desc);

	fpanic_status(fvfs_open("/subdir", fvfs_descriptor_flag_read, &desc));

	fvfs_descriptor_t* desc2;

	fpanic_status(fvfs_open_r(desc, "./.././test2.txt", fvfs_descriptor_flag_read, &desc2));

	fconsole_logf("Successfully opened descriptor using relative path!\n");

	char buf[5] = {0};
	size_t offset = 0;
	size_t read_count = 0;
	while (fvfs_read(desc2, offset, buf, sizeof(buf) - 1, &read_count) == ferr_ok) {
		fconsole_logf("Chunk: %.*s\n", (int)read_count, buf);
		offset += read_count;
	}

	fvfs_release(desc2);

	fvfs_release(desc);
#endif

	fpage_space_t space1;
	fpage_space_t space2;
	volatile uint8_t* virt1;
	volatile uint8_t* virt2;

	fpanic_status(fpage_space_init(&space1));
	fconsole_log("Initialized space 1\n");

	fpanic_status(fpage_space_init(&space2));
	fconsole_log("Initialized space 2\n");

	fpanic_status(fpage_space_allocate(&space1, 1, (void*)&virt1, 0));
	fconsole_logf("Allocate within space 1: %p\n", virt1);

	fpanic_status(fpage_space_allocate(&space2, 1, (void*)&virt2, 0));
	fconsole_logf("Allocated within space 2: %p\n", virt2);

	fpanic_status(fpage_space_swap(&space1));
	fconsole_log("Switched to space 1\n");

	*virt1 = 1;
	fconsole_log("Wrote within space 1\n");

	fpanic_status(fpage_space_swap(&space2));
	fconsole_log("Switched to space 2\n");

	*virt2 = 2;
	fconsole_log("Wrote within space 2\n");

	fpanic_status(fpage_space_swap(&space1));
	fconsole_log("Switched to space 1\n");

	fassert(*virt1 == 1);
	fconsole_log("Read from space 1 correctly\n");

	fpanic_status(fpage_space_swap(&space2));
	fconsole_log("Switched to space 2\n");

	fassert(*virt2 == 2);
	fconsole_log("Read from space 2 correctly\n");

	fpanic_status(fpage_space_free(&space1, (void*)virt1, 1));
	fconsole_log("Freed back to space 1\n");

	fpanic_status(fpage_space_free(&space2, (void*)virt2, 1));
	fconsole_log("Freed back to space 2\n");

	fpage_space_destroy(&space1);
	fconsole_log("Destroyed space 1\n");

	fpage_space_destroy(&space2);
	fconsole_log("Destroyed space 2\n");
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
	facpi_rsdp_t* rsdp = NULL;
	fthread_t* main_thread = NULL;
	void* stack_base = NULL;
	const char* config_data = NULL;
	size_t config_data_length = 0;

	const char* console_config = NULL;
	size_t console_config_length = 0;

	const char* debug_config = NULL;
	size_t debug_config_length = 0;

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

	// finally, fully switch to the higher-half by jumping into the new virtual instruction pointer
	fentry_jump_to_virtual(&&jump_here_for_virtual);
jump_here_for_virtual:;

	// interrupts are already disabled, but let our interrupt handler code know that
	fint_disable();

	// map basic regions we need to continue with our setup
	map_regions(&next_l2, &memory_map, memory_map_length, &initial_pool, initial_pool_page_count, &boot_data, boot_data_count, image_base, image_size);

	// initialize the paging subsystem so that we can start paging freely
	fpage_init(next_l2, &page_table_level_4, memory_map, memory_map_length, image_base);

	for (size_t i = 0; i < boot_data_count; ++i) {
		ferro_boot_data_info_t* curr = &boot_data[i];
		if (curr->type == ferro_boot_data_type_framebuffer_info) {
			fb_info = curr->virtual_address;
		} else if (curr->type == ferro_boot_data_type_rsdp_pointer) {
			rsdp = curr->physical_address;
		} else if (curr->type == ferro_boot_data_type_config) {
			config_data = curr->virtual_address;
			config_data_length = curr->size;
		} else if (curr->type == ferro_boot_data_type_ramdisk) {
			ramdisk = curr->virtual_address;
		}
	}

	// map the framebuffer
	if (fpage_map_kernel_any(fb_info->base, round_up_div(fb_info->scan_line_size * fb_info->height, FPAGE_PAGE_SIZE), &fb_info->base, 0) == ferr_ok) {
		ferro_fb_init(fb_info);
	}

	// initialize the console subsystem
	fconsole_init();

	if (config_data) {
		fconfig_init(config_data, config_data_length);
	}

	console_config = fconfig_get_nocopy("console", &console_config_length);
	debug_config = fconfig_get_nocopy("debug", &debug_config_length);

	// initialize the interrupts subsystem
	fint_init();

	// initialize the ACPI subsystem
	facpi_init(rsdp);

#if FERRO_ARCH == FERRO_ARCH_x86_64
	farch_tsc_init();
	farch_apic_init();
#elif FERRO_ARCH == FERRO_ARCH_aarch64
	farch_gic_init();
	farch_generic_timer_init();
#endif

	fserial_init();

	if (console_config) {
		fserial_t* serial_port = NULL;

		if (console_config_length == 7 && strncmp(console_config, "serial", 6) == 0 && console_config[6] >= '1' && console_config[6] <= '4') {
			serial_port = fserial_find(console_config[6] - '1');
		}

		if (serial_port) {
			fconsole_init_serial(serial_port);
		}
	}

	if (debug_config) {
		fserial_t* serial_port = NULL;

		if (debug_config_length == 7 && strncmp(debug_config, "serial", 6) == 0 && debug_config[6] >= '1' && debug_config[6] <= '4') {
			serial_port = fserial_find(debug_config[6] - '1');
		}

		if (serial_port) {
			fgdb_init(serial_port);
		}
	}

	stack_base = __builtin_frame_address(0);
	stack_base = (void*)round_down_power_of_2((uintptr_t)stack_base, FPAGE_LARGE_PAGE_SIZE);

	if (fthread_new(ferro_entry_threaded, NULL, stack_base, FPAGE_LARGE_PAGE_SIZE, 0, &main_thread) != ferr_ok) {
		fpanic("Failed to create main kernel thread");
	}

	// once we enter the scheduler, this function is gone
	fsched_init(main_thread);

	__builtin_unreachable();
};

