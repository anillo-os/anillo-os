#include <ferro/base.h>
#include <ferro/core/memory-regions.h>
#include <ferro/ramdisk.h>
#include <ferro/core/entry.h>
#include <ferro/bits.h>

#include <ferro/bootstrap/uefi/wrappers.h>

#define DEFAULT_KERNEL_PATH  "EFI\\anillo\\ferro"
#define DEFAULT_RAMDISK_PATH "EFI\\anillo\\ramdisk"
#define DEFAULT_CONFIG_PATH  "EFI\\anillo\\config.txt"

#define EXTRA_MM_DESCRIPTOR_COUNT 4

#define round_up_div(value, multiple) ({ \
		__typeof__(value) _value = (value); \
		__typeof__(multiple) _multiple = (multiple); \
		(_value + (_multiple - 1)) / _multiple; \
	})

FERRO_ALWAYS_INLINE ferro_memory_region_type_t uefi_to_ferro_memory_region_type(fuefi_memory_type_t uefi) {
	switch (uefi) {
		case fuefi_memory_type_loader_code:
		case fuefi_memory_type_loader_data:
		case fuefi_memory_type_bs_code:
		case fuefi_memory_type_bs_data:
		case fuefi_memory_type_generic:
			return ferro_memory_region_type_general;

		case fuefi_memory_type_nvram:
			return ferro_memory_region_type_nvram;

		case fuefi_memory_type_reserved:
		case fuefi_memory_type_rs_code:
		case fuefi_memory_type_rs_data:
		case fuefi_memory_type_unusable:
		case fuefi_memory_type_acpi:
		case fuefi_memory_type_mmio:
		case fuefi_memory_type_mmio_port_space:
			return ferro_memory_region_type_hardware_reserved;

		case fuefi_memory_type_acpi_reclaimable:
			return ferro_memory_region_type_acpi_reclaim;

		case fuefi_memory_type_processor_reserved:
			return ferro_memory_region_type_pal_code;

		default:
			// anything else is just an invalid value
			return ferro_memory_region_type_none;
	}
};

#define ROUND_UP_PAGE(x) (((x) + 0xfff) / 0x1000)

typedef struct ferro_memory_pool ferro_memory_pool_t;
struct ferro_memory_pool {
	void* base_address;
	size_t page_count;
	void* next_address;
};

static ferr_t ferro_memory_pool_init(ferro_memory_pool_t* pool, size_t pool_size) {
	pool->page_count = ROUND_UP_PAGE(pool_size);
	pool->next_address = pool->base_address = mmap(NULL, pool->page_count * 0x1000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
	return (pool->base_address == MAP_FAILED) ? ferr_temporary_outage : ferr_ok;
};

static void* ferro_memory_pool_allocate(ferro_memory_pool_t* pool, size_t bytes) {
	void* ret = pool->next_address;
	pool->next_address = (void*)((uintptr_t)pool->next_address + bytes);
	return ret;
};

// from https://stackoverflow.com/a/9194117/6620880
FERRO_ALWAYS_INLINE uint64_t round_up_power_of_2(uint64_t number, uint64_t multiple) {
	return (number + multiple - 1) & -multiple;
};

fuefi_status_t FUEFI_API efi_main(fuefi_handle_t image_handle, fuefi_system_table_t* system_table) {
	fuefi_status_t status = fuefi_status_load_error;
	int err = 0;

	size_t ferro_map_size = 0;
	ferro_memory_region_t* ferro_memory_map = NULL;
	size_t map_entry_count = 0;

	FILE* ramdisk_file = NULL;
	ferro_ramdisk_header_t local_ramdisk_header = {0};
	void* ramdisk_address = NULL;
	size_t ramdisk_size = 0;

	FILE* config_file = NULL;
	char* config_data = NULL;
	size_t config_data_size = 0x1000; // 4 KiB; a single 4KiB page should be more than enough for our configuration file
	size_t config_data_length = config_data_size;

	FILE* kernel_file = NULL;
	ferro_kernel_image_info_t* kernel_image_info = NULL;
	ferro_elf_header_t kernel_header = {0};
	ferro_elf_program_header_t* kernel_program_headers = NULL;
	uint64_t kernel_program_headers_byte_size = 0;
	char* kernel_program_headers_end = NULL;
	size_t kernel_segment_index = 0;
	size_t kernel_loadable_segment_count = 0;
	uintptr_t kernel_start_phys = 0;
	uintptr_t kernel_end_phys = 0;
	void* kernel_image_base = NULL;
	ferro_entry_f kernel_entry = NULL;

	bool graphics_available = false;
	ferro_fb_info_t* ferro_framebuffer_info = NULL;

	size_t ferro_pool_size = 0;
	ferro_memory_pool_t ferro_pool;

	ferro_boot_data_info_t* ferro_boot_data = NULL;
	size_t ferro_boot_data_count = 0;

	int mib[2] = {0};
	size_t sysctl_old_len = 0;
	fuefi_sysctl_wrappers_init_t init_info = {
		.image_handle = image_handle,
		.system_table = system_table,
	};
	fuefi_sysctl_bs_memory_map_info_t mm_info = {0};
	fuefi_sysctl_bs_populate_memory_map_t populate_mm_info = {0};

	uintptr_t rsdp_pointer = 0;

	mib[0] = CTL_WRAPPERS;
	mib[1] = WRAPPERS_INIT;
	if (sysctl(mib, 2, NULL, NULL, &init_info, sizeof(init_info)) < 0) {
		status = errstat;
		err = errno;
		return status;
	}

	printf("Info: Initializing Ferro bootstrap...\n");

	//printf("Info: UEFI image base: %p\n", (void*)(uintptr_t)sysconf(_SC_IMAGE_BASE));

	graphics_available = sysconf(_SC_FB_AVAILABLE);
	if (graphics_available) {
		ferro_pool_size += sizeof(ferro_fb_info_t);
		++ferro_boot_data_count;
	} else {
		printf("Warning: No framebuffer available. Ferro will not be able to output early logging messages.\n");
	}

	// try to open our config file
	if ((config_file = fopen(DEFAULT_CONFIG_PATH, "rb")) == NULL) {
		printf("Warning: Failed to find/open Anillo OS bootloader configuration file (\"efi:\\" DEFAULT_CONFIG_PATH "\").\n");
		config_file = NULL;
	} else {
		printf("Info: Opened configuration file\n");
		ferro_pool_size += config_data_size;
		++ferro_boot_data_count;
	}

	// try to open our ramdisk file
	if ((ramdisk_file = fopen(DEFAULT_RAMDISK_PATH, "rb")) == NULL) {
		printf("Warning: Failed to find/open Anillo OS ramdisk (\"efi:\\" DEFAULT_RAMDISK_PATH "\").\n");
		ramdisk_file = NULL;
	} else {
		printf("Info: Opened ramdisk\n");
	}

	// open our kernel
	if ((kernel_file = fopen(DEFAULT_KERNEL_PATH, "rb")) == NULL) {
		status = errstat;
		err = errno;
		printf("Error: Failed to find/open Anillo OS kernel (\"efi:\\" DEFAULT_KERNEL_PATH "\"; status=" FUEFI_STATUS_FORMAT "; err=%d).\n", status, err);
		return status;
	}
	printf("Info: Opened kernel image\n");

	// load our ramdisk file
	if (ramdisk_file != NULL) {
		// read the header to determine the size of the ramdisk
		if (fread(&local_ramdisk_header, sizeof(ferro_ramdisk_header_t), 1, ramdisk_file) != sizeof(ferro_ramdisk_header_t)) {
			printf("Warning: Failed to read ramdisk header.\n");
			fclose(ramdisk_file);
			ramdisk_file = NULL;
		} else {
			// success
			ramdisk_size = sizeof(ferro_ramdisk_header_t) + local_ramdisk_header.ramdisk_size;
			ferro_pool_size += ramdisk_size;
			++ferro_boot_data_count;
		}
	}

	// reserve space for the kernel image info structure
	ferro_pool_size += sizeof(ferro_kernel_image_info_t);
	++ferro_boot_data_count;

	// read in the ELF header
	if (fread(&kernel_header, sizeof(ferro_elf_header_t), 1, kernel_file) != sizeof(ferro_elf_header_t)) {
		status = errstat;
		err = errno;
		printf("Error: Failed to read kernel header (status=" FUEFI_STATUS_FORMAT "; err=%d).\n", status, err);
		return status;
	}
	printf("Info: Read kernel ELF header\n");

	// verify the kernel image
	if (
		kernel_header.magic != FERRO_ELF_MAGIC ||
		kernel_header.bits != ferro_elf_bits_64 ||
		kernel_header.identifier_version != FERRO_ELF_IDENTIFIER_VERSION ||
		kernel_header.abi != ferro_elf_abi_sysv ||
		kernel_header.type != ferro_elf_type_executable ||
		kernel_header.format_version != FERRO_ELF_FORMAT_VERSION
	) {
		status = errstat;
		err = errno;
		printf("Error: Failed to verify kernel image header (status=" FUEFI_STATUS_FORMAT "; err=%d).\n", status, err);
		return status;
	}
	printf("Info: Found valid kernel image\n");

	printf("Info: Kernel entry address: %llx\n", kernel_header.entry);

	kernel_program_headers_byte_size = kernel_header.program_header_entry_size * kernel_header.program_header_entry_count;

	// set the file position to the program header table
	if (fseek(kernel_file, kernel_header.program_header_table_offset, SEEK_SET) != 0) {
		status = errstat;
		err = errno;
		printf("Error: Failed to set kernel file read offset (status=" FUEFI_STATUS_FORMAT "; err=%d).\n", status, err);
		return status;
	}
	printf("Info: Set kernel read offset (program header table)\n");

	// allocate space for the program header table
	if ((kernel_program_headers = malloc(kernel_program_headers_byte_size)) == NULL) {
		status = errstat;
		err = errno;
		printf("Error: Failed to allocate memory for program header table (status=" FUEFI_STATUS_FORMAT "; err=%d).\n", status, err);
		return status;
	}
	kernel_program_headers_end = (char*)kernel_program_headers + kernel_program_headers_byte_size;
	printf("Info: Allocated program header table\n");

	// read the program header table
	if (fread(kernel_program_headers, kernel_program_headers_byte_size, 1, kernel_file) != kernel_program_headers_byte_size) {
		status = errstat;
		err = errno;
		printf("Error: Failed to read program header table for program header table (status=" FUEFI_STATUS_FORMAT "; err=%d).\n", status, err);
		return status;
	}
	printf("Info: Read program header table\n");

	// determine the number of loadable segments
	for (ferro_elf_program_header_t* program_header = kernel_program_headers; (char*)program_header < kernel_program_headers_end; program_header = (ferro_elf_program_header_t*)((char*)program_header + kernel_header.program_header_entry_size)) {
		if (program_header->type == ferro_elf_program_header_type_loadable) {
			++kernel_loadable_segment_count;
			ferro_pool_size += sizeof(ferro_kernel_segment_t);
		}
	}
	printf("Info: Number of loadable kernel segments: %zu\n", kernel_loadable_segment_count);

	// reserve space for a boot data entry for the segment information table
	++ferro_boot_data_count;
	// and another for the memory map
	++ferro_boot_data_count;
	// and another for the initial pool
	++ferro_boot_data_count;

	rsdp_pointer = sysconf(_SC_ACPI_RSDP);
	if (rsdp_pointer) {
		++ferro_boot_data_count;
	}

	// reserve pool space for the boot data array
	ferro_pool_size += sizeof(ferro_boot_data_info_t) * ferro_boot_data_count;

	// allocate Ferro's initial memory pool
	if (ferro_memory_pool_init(&ferro_pool, ferro_pool_size) != ferr_ok) {
		status = fuefi_status_out_of_resources;
		printf("Error: Failed to allocate initial Ferro memory pool.\n");
		return status;
	}

	// now we can do all the things that have been waiting for the pool

	// populate the framebuffer information
	if (graphics_available) {
		#define unsigned_sysconf(name) ({ \
				long long result = sysconf(name); \
				*(unsigned long long*)&result; \
			})

		if ((ferro_framebuffer_info = ferro_memory_pool_allocate(&ferro_pool, sizeof(ferro_fb_info_t))) == NULL) {
			status = errstat;
			err = errno;
			printf("Error: Failed to allocate memory for Ferro framebuffer information structure (status=" FUEFI_STATUS_FORMAT "; err=%d).\n", status, err);
			return status;
		}
		printf("Info: Allocated space for Ferro framebuffer information structure.\n");

		ferro_framebuffer_info->base = (void*)unsigned_sysconf(_SC_FB_BASE);
		ferro_framebuffer_info->width = unsigned_sysconf(_SC_FB_WIDTH);
		ferro_framebuffer_info->height = unsigned_sysconf(_SC_FB_HEIGHT);
		ferro_framebuffer_info->pixel_bits = unsigned_sysconf(_SC_FB_BIT_COUNT);
		ferro_framebuffer_info->red_mask = unsigned_sysconf(_SC_FB_RED_MASK);
		ferro_framebuffer_info->green_mask = unsigned_sysconf(_SC_FB_GREEN_MASK);
		ferro_framebuffer_info->blue_mask = unsigned_sysconf(_SC_FB_BLUE_MASK);
		ferro_framebuffer_info->other_mask = unsigned_sysconf(_SC_FB_RESERVED_MASK);

		// note that we assume we're dealing with a sane implementation that doesn't try to squeeze the most out of it's memory by packing partial pixels into the same byte
		// (e.g. if using 15bpp, it won't try to use *exactly* 15 bits, it'll pad to 16 instead and use 2 bytes per pixel)
		ferro_framebuffer_info->scan_line_size = round_up_div(ferro_framebuffer_info->pixel_bits, 8U) * unsigned_sysconf(_SC_FB_PIXELS_PER_SCANLINE);

		printf("Info: Finished determining graphics framebuffer information.\n");
	}

	// read our config file
	if (config_file != NULL) {
		if ((config_data = ferro_memory_pool_allocate(&ferro_pool, config_data_size)) == NULL) {
			printf("Warning: Failed to allocate memory for config data.\n");
		} else {
			// success
			printf("Info: Allocated memory for configuration file\n");

			// zero out the memory
			memset(config_data, 0, config_data_size);

			// actually read the data
			config_data_length = fread(config_data, config_data_length, 1, config_file);
			printf("Info: Read configuration file into memory\n");
		}
	}

	// load in our ramdisk
	if (ramdisk_file != NULL) {
		if ((ramdisk_address = ferro_memory_pool_allocate(&ferro_pool, ramdisk_size)) == NULL) {
			printf("Warning: Failed to allocate memory for ramdisk contents.\n");
		} else {
			// success
			printf("Info: Allocated memory for ramdisk\n");

			// zero out the memory
			memset(ramdisk_address, 0, ramdisk_size);

			// copy in the header
			memcpy(ramdisk_address, &local_ramdisk_header, sizeof(ferro_ramdisk_header_t));

			ferro_ramdisk_header_t* ramdisk_header = ramdisk_address;

			// actually read the data
			if (fread(ramdisk_header->contents, ramdisk_size - sizeof(ferro_ramdisk_header_t), 1, ramdisk_file) != ramdisk_size - sizeof(ferro_ramdisk_header_t)) {
				printf("Warning: Failed to read ramdisk contents.\n");
				ramdisk_address = NULL;
			} else {
				printf("Info: Read ramdisk into memory\n");
			}
		}
	}

	// allocate space for the kernel image info structure
	if ((kernel_image_info = ferro_memory_pool_allocate(&ferro_pool, sizeof(ferro_kernel_image_info_t))) == NULL) {
		status = errstat;
		err = errno;
		printf("Error: Failed to allocate memory for kernel image information (status=" FUEFI_STATUS_FORMAT "; err=%d).\n", status, err);
		return status;
	}
	printf("Info: Allocated space for kernel image info structure\n");

	// set the number of loadable segments
	kernel_image_info->segment_count = kernel_loadable_segment_count;

	// allocate space for the segment info array
	if ((kernel_image_info->segments = ferro_memory_pool_allocate(&ferro_pool, sizeof(ferro_kernel_segment_t) * kernel_image_info->segment_count)) == NULL) {
		status = errstat;
		err = errno;
		printf("Error: Failed to allocate memory for kernel segment information table (status=" FUEFI_STATUS_FORMAT "; err=%d).\n", status, err);
		return status;
	}
	printf("Info: Allocated segment information array\n");

	// determine how big of a block to allocate
	for (ferro_elf_program_header_t* program_header = kernel_program_headers; (char*)program_header < kernel_program_headers_end; program_header = (ferro_elf_program_header_t*)((char*)program_header + kernel_header.program_header_entry_size)) {
		if (program_header->type == ferro_elf_program_header_type_loadable) {
			if (program_header->physical_address < kernel_start_phys) {
				kernel_start_phys = program_header->physical_address;
			}

			printf("Info: phys = 0x%zx, end = 0x%zx\n", program_header->physical_address, program_header->physical_address + program_header->memory_size);

			if (program_header->physical_address + program_header->memory_size > kernel_end_phys) {
				kernel_end_phys = program_header->physical_address + program_header->memory_size;
			}
		}
	}

	// align the end address
	kernel_end_phys = ROUND_UP_PAGE(kernel_end_phys) * 0x1000;

	// update the kernel image info
	kernel_image_info->size = kernel_end_phys - kernel_start_phys;

	printf("Info: kernel image size = %zu (%zx); requested size for alignment = %zu (%zx)\n", kernel_image_info->size, kernel_image_info->size, kernel_image_info->size + 0x1fffffULL, kernel_image_info->size + 0x1fffffULL);

	// allocate the block
	// this is a little more complicated than just allocating some pages because our kernel requires 2MiB alignment
	// if we request almost an entire 2MiB page than what we really need, we're guaranteed to have a 2MiB boundary somewhere in there
	if ((kernel_image_base = mmap(NULL, kernel_image_info->size + 0x1fffffULL, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON, -1, 0)) == MAP_FAILED) {
		status = errstat;
		err = errno;
		printf("Error: Failed to allocate memory for kernel image (status=" FUEFI_STATUS_FORMAT "; err=%d).\n", status, err);
		return status;
	}

	printf("Info: got region at %p; going to unmap and try getting %p\n", kernel_image_base, (void*)round_up_power_of_2((uintptr_t)kernel_image_base, 0x200000ULL));

	// now unmap the region...
	if (munmap(kernel_image_base, kernel_image_info->size + 0x1fffffULL) != 0) {
		status = errstat;
		err = errno;
		printf("Error: Failed to unmap temporary memory for kernel image (status=" FUEFI_STATUS_FORMAT "; err=%d).\n", status, err);
		return status;
	}

	// ...align the address...
	kernel_image_base = (void*)round_up_power_of_2((uintptr_t)kernel_image_base, 0x200000ULL);

	// ...and allocate one at the address we want with exactly the size we want
	if ((kernel_image_base = mmap(kernel_image_base, kernel_image_info->size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0)) == MAP_FAILED) {
		status = errstat;
		err = errno;
		printf("Error: Failed to allocate 2MiB-aligned memory for kernel image (status=" FUEFI_STATUS_FORMAT "; err=%d).\n", status, err);
		return status;
	}

	// update the kernel entry address
	kernel_entry = kernel_header.entry + kernel_image_base;

	// and the kernel image info
	kernel_image_info->physical_base_address = kernel_image_base;

	// actually read in the segments
	for (ferro_elf_program_header_t* program_header = kernel_program_headers; (char*)program_header < kernel_program_headers_end; program_header = (ferro_elf_program_header_t*)((char*)program_header + kernel_header.program_header_entry_size)) {
		if (program_header->type == ferro_elf_program_header_type_loadable) {
			ferro_kernel_segment_t* segment = &kernel_image_info->segments[kernel_segment_index];

			segment->size = program_header->memory_size;
			segment->physical_address = (void*)(uintptr_t)(program_header->physical_address - kernel_start_phys + kernel_image_base);
			segment->virtual_address = (void*)(uintptr_t)(program_header->virtual_address);

			// zero out the memory
			memset(segment->physical_address, 0, segment->size);

			// set the file position to read the segment
			if (fseek(kernel_file, program_header->offset, SEEK_SET) != 0) {
				status = errstat;
				err = errno;
				printf("Error: Failed to set kernel file read offset for segment (status=" FUEFI_STATUS_FORMAT "; err=%d).\n", status, err);
				return status;
			}

			// actually read in the segment
			if (fread(segment->physical_address, program_header->file_size, 1, kernel_file) != program_header->file_size) {
				status = errstat;
				err = errno;
				printf("Error: Failed to read kernel segment (status=" FUEFI_STATUS_FORMAT "; err=%d).\n", status, err);
				return status;
			}

			printf("Info: Read in section to physical address %p and virtual address %p.\n", segment->physical_address, segment->virtual_address);

			++kernel_segment_index;
		}
	}
	printf("Info: Loaded %zu kernel segments\n", kernel_segment_index);

	// get the required size for the UEFI memory map
	printf("Info: Determining required size for memory map...\n");
	mib[0] = CTL_BS;
	mib[1] = BS_MEMORY_MAP_INFO;
	sysctl_old_len = sizeof(mm_info);
	if (sysctl(mib, 2, &mm_info, &sysctl_old_len, NULL, 0) < 0) {
		status = errstat;
		err = errno;
		printf("Error: Failed to determine required memory map size (status=" FUEFI_STATUS_FORMAT "; err=%d).\n", status, err);
		return status;
	}
	map_entry_count = mm_info.map_size / mm_info.descriptor_size;
	printf("Info: Initial UEFI memory map size: %zu (count=%zu)\n", mm_info.map_size, map_entry_count);

	// account for additional descriptors that may need to be created for the allocation of the UEFI memory map as well as our own memory map for Ferro
	mm_info.map_size += EXTRA_MM_DESCRIPTOR_COUNT * mm_info.descriptor_size;

	// allocate the UEFI memory map
	if ((populate_mm_info.memory_map = malloc(mm_info.map_size)) == NULL) {
		status = errstat;
		err = errno;
		printf("Error: Failed to allocate memory to store UEFI memory map (status=" FUEFI_STATUS_FORMAT "; err=%d).\n", status, err);
		return status;
	}
	printf("Info: Allocated UEFI memory map\n");

	// allocate a memory map for Ferro
	// `+ 4` so we can map the memory map and initial pool as their own entries marked as "kernel reserved"
	// and we add the kernel segment count multiplied by two so we can map them as kernel reserved as well
	ferro_map_size = ((mm_info.map_size / mm_info.descriptor_size) + 4 + (kernel_image_info->segment_count * 2)) * sizeof(ferro_memory_region_t);
	if ((ferro_memory_map = mmap(NULL, ROUND_UP_PAGE(ferro_map_size) * 0x1000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0)) == MAP_FAILED) {
		status = errstat;
		err = errno;
		printf("Error: Failed to allocate memory to store Ferro memory map (status=" FUEFI_STATUS_FORMAT "; err=%d).\n", status, err);
		return status;
	}
	printf("Info: Allocated Ferro memory map\n");
	memset(ferro_memory_map, 0, ferro_map_size);

	// can't call `printf` anymore after acquiring the memory map; it might allocate more memory and mess up the memory map
	printf("Info: Going to acquire final UEFI memory map (no more UEFI-based messages after this point, except for fatal errors)\n");
	// populate the UEFI memory map
	mib[0] = CTL_BS;
	mib[1] = BS_POPULATE_MEMORY_MAP;
	sysctl_old_len = sizeof(populate_mm_info);
	populate_mm_info.map_size = mm_info.map_size;
	if (sysctl(mib, 2, &populate_mm_info, &sysctl_old_len, NULL, 0) < 0) {
		status = errstat;
		err = errno;
		printf("Error: Failed to populate UEFI memory map (status=" FUEFI_STATUS_FORMAT "; err=%d).\n", status, err);
		return status;
	}
	map_entry_count = mm_info.map_size / mm_info.descriptor_size;
	//printf("Info: Final UEFI memory map size: %zu (count=%zu)\n", MapSize, MapEntryCount);
	//printf("Info: Memory map key is %zu\n", MapKey);

	// populate the Ferro memory map
	for (size_t i = 0; i < map_entry_count; ++i) {
		fuefi_memory_descriptor_t* descriptor = (fuefi_memory_descriptor_t*)((uintptr_t)populate_mm_info.memory_map + (i * mm_info.descriptor_size));
		ferro_memory_region_t* ferro_region = &ferro_memory_map[i];

		ferro_region->type = uefi_to_ferro_memory_region_type(descriptor->type);
		ferro_region->physical_start = (uintptr_t)descriptor->physical_start;
		ferro_region->virtual_start = 0;
		ferro_region->page_count = descriptor->page_count;
	}
	//printf("Info: Populated Ferro memory map\n");

	// override types for special addresses
	for (size_t i = 0; i < 3; ++i) {
		uintptr_t physical_address = 0;
		uintptr_t virtual_address = 0;
		size_t page_count = 0;

		if (i == 0) {
			physical_address = (uintptr_t)ferro_memory_map;
			page_count = ROUND_UP_PAGE(ferro_map_size);
		} else if (i == 1) {
			physical_address = (uintptr_t)ferro_pool.base_address;
			page_count = ROUND_UP_PAGE(ferro_pool_size);
		} else {
			physical_address = (uintptr_t)kernel_image_base;
			page_count = ROUND_UP_PAGE(kernel_image_info->size);
		}

		if (page_count == 0) {
			continue;
		}

		for (size_t j = 0; j < map_entry_count; ++j) {
			ferro_memory_region_t* ferro_region = &ferro_memory_map[j];

			if (physical_address > ferro_region->physical_start && physical_address < ferro_region->physical_start + (ferro_region->page_count * 0x1000)) {
				ferro_memory_region_t* new_ferro_region = &ferro_memory_map[j + 1];
				for (size_t k = map_entry_count; k > j; --k) {
					memcpy(&ferro_memory_map[k], &ferro_memory_map[k - 1], sizeof(ferro_memory_region_t));
				}
				++map_entry_count;
				new_ferro_region->physical_start = (uintptr_t)physical_address;
				ferro_region->page_count = ROUND_UP_PAGE(new_ferro_region->physical_start - ferro_region->physical_start);
				new_ferro_region->page_count -= ferro_region->page_count;
				new_ferro_region->type = ferro_region->type;
				ferro_region = new_ferro_region;
				++j;
			}

			if ((uintptr_t)physical_address == ferro_region->physical_start) {
				if (ferro_region->page_count > page_count) {
					// we have to create a new entry for the remaining memory
					ferro_memory_region_t* new_ferro_region = &ferro_memory_map[j + 1];
					for (size_t k = map_entry_count; k > j; --k) {
						memcpy(&ferro_memory_map[k], &ferro_memory_map[k - 1], sizeof(ferro_memory_region_t));
					}
					++map_entry_count;
					new_ferro_region->page_count = ferro_region->page_count - page_count;
					new_ferro_region->physical_start = ferro_region->physical_start + (page_count * 0x1000);
					new_ferro_region->type = ferro_region->type;
				}
				ferro_region->type = ferro_memory_region_type_kernel_reserved;
				ferro_region->virtual_start = virtual_address;
				ferro_region->page_count = page_count;
				break;
			}
		}
	}
	//printf("Info: Added kernel segment information to Ferro memory map\n");

	// allocate the boot data information array
	if ((ferro_boot_data = ferro_memory_pool_allocate(&ferro_pool, sizeof(ferro_boot_data_info_t) * ferro_boot_data_count)) == NULL) {
		status = errstat;
		err = errno;
		printf("Error: Failed to allocate memory for boot data information array (status=" FUEFI_STATUS_FORMAT "; err=%d).\n", status, err);
		return status;
	}
	//printf("Info: Allocated space for boot data information array.\n");

	// populate the boot data information array
	{
		size_t i = 0;
		ferro_boot_data_info_t* info = NULL;

		info = &ferro_boot_data[i++];
		info->physical_address = kernel_image_info;
		info->size = sizeof(ferro_kernel_image_info_t);
		info->virtual_address = NULL;
		info->type = ferro_boot_data_type_kernel_image_info;

		info = &ferro_boot_data[i++];
		info->physical_address = kernel_image_info->segments;
		info->size = kernel_image_info->segment_count * sizeof(ferro_kernel_segment_t);
		info->virtual_address = NULL;
		info->type = ferro_boot_data_type_kernel_segment_info_table;

		info = &ferro_boot_data[i++];
		info->physical_address = ferro_memory_map;
		info->size = ferro_map_size;
		info->virtual_address = NULL;
		info->type = ferro_boot_data_type_memory_map;

		info = &ferro_boot_data[i++];
		info->physical_address = ferro_pool.base_address;
		info->size = ferro_pool.page_count * 0x1000;
		info->virtual_address = NULL;
		info->type = ferro_boot_data_type_initial_pool;

		if (graphics_available) {
			info = &ferro_boot_data[i++];
			info->physical_address = ferro_framebuffer_info;
			info->size = sizeof(ferro_fb_info_t);
			info->virtual_address = NULL;
			info->type = ferro_boot_data_type_framebuffer_info;
		}

		if (config_data != NULL) {
			info = &ferro_boot_data[i++];
			info->physical_address = config_data;
			info->size = config_data_size;
			info->virtual_address = NULL;
			info->type = ferro_boot_data_type_config;
		}

		if (ramdisk_address != NULL) {
			info = &ferro_boot_data[i++];
			info->physical_address = ramdisk_address;
			info->size = ramdisk_size;
			info->virtual_address = NULL;
			info->type = ferro_boot_data_type_ramdisk;
		}

		if (rsdp_pointer) {
			info = &ferro_boot_data[i++];
			info->physical_address = (void*)rsdp_pointer;
			info->size = sizeof(facpi_rsdp_t);
			info->virtual_address = NULL;
			info->type = ferro_boot_data_type_rsdp_pointer;
		}
	};

	// almost there: call ExitBootServices to prepare for kernel handoff
	//printf("Info: Going to exit boot services\n");
	mib[0] = CTL_BS;
	mib[1] = BS_EXIT_BOOT_SERVICES;
	if (sysctl(mib, 2, NULL, NULL, &populate_mm_info.map_key, sizeof(populate_mm_info.map_key)) < 0) {
		status = errstat;
		err = errno;
		printf("Error: Failed to terminate boot services (status=" FUEFI_STATUS_FORMAT "; err=%d).\n", status, err);
		return status;
	}

	// finally, jump into our kernel
	kernel_entry(ferro_pool.base_address, ferro_pool.page_count, ferro_boot_data, ferro_boot_data_count);

	// if we get here, we failed to load the kernel
	return fuefi_status_load_error;
};
