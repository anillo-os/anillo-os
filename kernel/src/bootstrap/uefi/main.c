#include <efi.h>
#include <efilib.h>

#include <ferro/base.h>
#include <ferro/core/memory-regions.h>
#include <ferro/ramdisk.h>
#include <ferro/bootstrap/uefi/wrappers.h>
#include <ferro/core/entry.h>
#include <ferro/bits.h>

#define DEFAULT_KERNEL_PATH  L"EFI\\anillo\\ferro"
#define DEFAULT_RAMDISK_PATH L"EFI\\anillo\\ramdisk"
#define DEFAULT_CONFIG_PATH  L"EFI\\anillo\\config.txt"

// really, gnu-efi? you provide UINTN but no format string specifier for it?
#if __x86_64__ || __aarch64__
	#define UINTN_FORMAT "%lu"
#else
	#define UINTN_FORMAT "%u"
#endif

#define EFI_STATUS_FORMAT UINTN_FORMAT

extern void* memset(void* destination, int value, size_t count);
extern void* memcpy(void* destination, const void* source, size_t count);
#define max(a, b) ({ \
		__typeof__(a) _a = (a); \
		__typeof__(b) _b = (b); \
		(_a > _b) ? _a : _b; \
	})
#define round_up_div(value, multiple) ({ \
		__typeof__(value) _value = (value); \
		__typeof__(multiple) _multiple = (multiple); \
		(_value + (_multiple - 1)) / _multiple; \
	})

FERRO_INLINE ferro_memory_region_type_t uefi_to_ferro_memory_region_type(EFI_MEMORY_TYPE uefi) {
	switch (uefi) {
		case EfiLoaderCode:
		case EfiLoaderData:
		case EfiBootServicesCode:
		case EfiBootServicesData:
		case EfiConventionalMemory:
			return ferro_memory_region_type_general;

		case EfiReservedMemoryType:
		case EfiRuntimeServicesCode:
		case EfiRuntimeServicesData:
		case EfiUnusableMemory:
		case EfiACPIMemoryNVS:
		case EfiMemoryMappedIO:
		case EfiMemoryMappedIOPortSpace:
			return ferro_memory_region_type_hardware_reserved;

		case EfiACPIReclaimMemory:
			return ferro_memory_region_type_acpi_reclaim;

		case EfiPalCode:
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
	pool->next_address = pool->base_address = BSAllocatePages(BS, AllocateAnyPages, EfiLoaderData, pool->page_count, NULL);
	return pool->base_address == NULL ? ferr_temporary_outage : ferr_ok;
};

static void* ferro_memory_pool_allocate(ferro_memory_pool_t* pool, size_t bytes) {
	void* ret = pool->next_address;
	pool->next_address = (void*)((uintptr_t)pool->next_address + bytes);
	return ret;
};

EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE* SystemTable) {
	EFI_STATUS status = EFI_LOAD_ERROR;
	UINTN MapSize = 0;
	UINTN MapKey = 0;
	UINTN DescriptorSize = 0;
	EFI_MEMORY_DESCRIPTOR* MemoryMap = NULL;
	UINT32 DescriptorVersion = 0;
	UINTN FerroMapSize = 0;
	ferro_memory_region_t* FerroMemoryMap = NULL;
	UINTN MapEntryCount = 0;
	EFI_LOADED_IMAGE_PROTOCOL* ImageProtocol = NULL;
	EFI_GUID EfiLoadedImageProtocolGuid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
	EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* FSProtocol = NULL;
	EFI_GUID FSGuid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
	EFI_FILE* FSRoot = NULL;
	EFI_FILE* ConfigFile = NULL;
	EFI_FILE* KernelFile = NULL;
	EFI_FILE* RamdiskFile = NULL;
	void* RamdiskAddress = NULL;
	char* ConfigData = NULL;
	UINTN ConfigDataSize = 0x1000; // 4 KiB; a single 4KiB page should be more than enough for our configuration file
	UINTN ConfigDataLength = ConfigDataSize;
	ferro_kernel_image_info_t* KernelImageInfo = NULL;
	ferro_elf_header_t KernelHeader = {0};
	ferro_elf_program_header_t* KernelProgramHeaders = NULL;
	UINT64 KernelProgramHeadersByteSize = 0;
	char* KernelProgramHeadersEnd = NULL;
	UINTN KernelSegmentIndex = 0;
	EFI_GRAPHICS_OUTPUT_PROTOCOL* GraphicsProtocol = NULL;
	ferro_fb_info_t* FerroFramebufferInfo = NULL;
	size_t FerroPoolSize = 0;
	ferro_ramdisk_header_t LocalRamdiskHeader = {0};
	UINTN RamdiskSize = 0;
	UINTN KernelLoadableSegmentCount = 0;
	ferro_memory_pool_t FerroPool;
	ferro_boot_data_info_t* FerroBootData = NULL;
	UINTN FerroBootDataCount = 0;

	InitializeLib(ImageHandle, SystemTable);

	Print(L"Info: Initializing Ferro bootstrap...\n");

	if ((GraphicsProtocol = BSLocateProtocol(SystemTable->BootServices, (EFI_GUID)EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID)) == NULL) {
		Print(L"Warning: Failed to acquire graphics protocol.\n");
	} else if (GraphicsProtocol->Mode->Info->PixelFormat == PixelBltOnly) {
		Print(L"Warning: No framebuffer available. Ferro will not be able to output early logging messages.\n");
		GraphicsProtocol = NULL;
	} else {
		FerroPoolSize += sizeof(ferro_fb_info_t);
		++FerroBootDataCount;
	}

	// open the EFI system partition
	{
		if ((ImageProtocol = BSHandleProtocol(SystemTable->BootServices, ImageHandle, &EfiLoadedImageProtocolGuid)) == NULL) {
			Print(L"Error: Failed to retrieve image handle protocol.\n");
			return status;
		} else {
			Print(L"Info: Acquired EFI parition device protocol\n");
		}

		if ((FSProtocol = BSHandleProtocol(SystemTable->BootServices, ImageProtocol->DeviceHandle, &FSGuid)) == NULL) {
			Print(L"Error: Failed to retrieve EFI system partition FS protocol.\n");
			return status;
		} else {
			Print(L"Info: Opened EFI partition device\n");
		}

		if ((FSRoot = FSOpenVolume(FSProtocol)) == NULL) {
			Print(L"Error: Failed to open EFI system partition as FS volume.\n");
			return status;
		} else {
			Print(L"Info: Opened root EFI volume\n");
		}
	}

	// try to open our config file
	if ((ConfigFile = FileOpen(FSRoot, DEFAULT_CONFIG_PATH, EFI_FILE_MODE_READ, EFI_FILE_READ_ONLY)) == NULL) {
		Print(L"Warning: Failed to find/open Anillo OS bootloader configuration file (\"efi:\\" DEFAULT_CONFIG_PATH "\").\n");
		ConfigFile = NULL;
	} else {
		Print(L"Info: Opened configuration file\n");
		FerroPoolSize += ConfigDataSize;
		++FerroBootDataCount;
	}

	// try to open our ramdisk file
	if ((RamdiskFile = FileOpen(FSRoot, DEFAULT_RAMDISK_PATH, EFI_FILE_MODE_READ, EFI_FILE_READ_ONLY)) == NULL) {
		Print(L"Warning: Failed to find/open Anillo OS ramdisk (\"efi:\\" DEFAULT_RAMDISK_PATH "\").\n");
		RamdiskFile = NULL;
	} else {
		Print(L"Info: Opened ramdisk\n");
	}

	// open our kernel
	if ((KernelFile = FileOpen(FSRoot, DEFAULT_KERNEL_PATH, EFI_FILE_MODE_READ, EFI_FILE_READ_ONLY)) == NULL) {
		Print(L"Error: Failed to find/open Anillo OS kernel (\"efi:\\" DEFAULT_KERNEL_PATH "\").\n");
		return status;
	}
	Print(L"Info: Opened kernel image\n");

	// load our ramdisk file
	if (RamdiskFile != NULL) {
		// read the header to determine the size of the ramdisk
		if (FileRead(RamdiskFile, sizeof(ferro_ramdisk_header_t), &LocalRamdiskHeader) == FileReadError) {
			Print(L"Warning: Failed to read ramdisk header.\n");
			RamdiskFile = NULL;
		} else {
			// success
			RamdiskSize = sizeof(ferro_ramdisk_header_t) + LocalRamdiskHeader.ramdisk_size;
			FerroPoolSize += RamdiskSize;
			++FerroBootDataCount;
		}
	}

	// reserve space for the kernel image info structure
	FerroPoolSize += sizeof(ferro_kernel_image_info_t);
	++FerroBootDataCount;

	// read in the ELF header
	if (FileRead(KernelFile, sizeof(ferro_elf_header_t), &KernelHeader) == FileReadError) {
		Print(L"Error: Failed to read kernel header.\n");
		return status;
	}
	Print(L"Info: Read kernel ELF header\n");

	// verify the kernel image
	if (
		KernelHeader.magic != FERRO_ELF_MAGIC ||
		KernelHeader.bits != ferro_elf_bits_64 ||
		KernelHeader.identifier_version != FERRO_ELF_IDENTIFIER_VERSION ||
		KernelHeader.abi != ferro_elf_abi_sysv ||
		KernelHeader.type != ferro_elf_type_executable ||
		KernelHeader.format_version != FERRO_ELF_FORMAT_VERSION
	) {
		Print(L"Error: Failed to verify kernel image header.\n");
		return status;
	}
	Print(L"Info: Found valid kernel image\n");

	Print(L"Info: Kernel entry address: %lx\n", KernelHeader.entry);

	KernelProgramHeadersByteSize = KernelHeader.program_header_entry_size * KernelHeader.program_header_entry_count;

	// set the file position to the program header table
	if (FileSetPosition(KernelFile, KernelHeader.program_header_table_offset) != EFI_SUCCESS) {
		Print(L"Error: Failed to set kernel file read offset.\n");
		return status;
	}
	Print(L"Info: Set kernel read offset (program header table)\n");

	// allocate space for the program header table
	if ((KernelProgramHeaders = BSAllocatePool(SystemTable->BootServices, EfiLoaderData, KernelProgramHeadersByteSize)) == NULL) {
		Print(L"Error: Failed to allocate memory for program header table.\n");
		return status;
	}
	KernelProgramHeadersEnd = (char*)KernelProgramHeaders + KernelProgramHeadersByteSize;
	Print(L"Info: Allocated program header table\n");

	// read the program header table
	if (FileRead(KernelFile, KernelProgramHeadersByteSize, KernelProgramHeaders) == FileReadError) {
		Print(L"Error: Failed to read program header table for program header table.\n");
		return status;
	}
	Print(L"Info: Read program header table\n");

	// determine the number of loadable segments
	for (ferro_elf_program_header_t* ProgramHeader = KernelProgramHeaders; (char*)ProgramHeader < KernelProgramHeadersEnd; ProgramHeader = (ferro_elf_program_header_t*)((char*)ProgramHeader + KernelHeader.program_header_entry_size)) {
		if (ProgramHeader->type == ferro_elf_program_header_type_loadable) {
			++KernelLoadableSegmentCount;
			FerroPoolSize += sizeof(ferro_kernel_segment_t);
		}
	}
	Print(L"Info: Number of loadable kernel segments: " UINTN_FORMAT "\n", KernelLoadableSegmentCount);

	// reserve space for a boot data entry for the segment information table
	++FerroBootDataCount;
	// and another for the memory map
	++FerroBootDataCount;
	// and another for the initial pool
	++FerroBootDataCount;

	// reserve pool space for the boot data array
	FerroPoolSize += sizeof(ferro_boot_data_info_t) * FerroBootDataCount;

	// allocate Ferro's initial memory pool
	if (ferro_memory_pool_init(&FerroPool, FerroPoolSize) != ferr_ok) {
		status = EFI_OUT_OF_RESOURCES;
		Print(L"Error: Failed to allocate initial Ferro memory pool.\n");
		return status;
	}

	// now we can do all the things that have been waiting for the pool

	// populate the framebuffer information
	if (GraphicsProtocol != NULL) {
		if ((FerroFramebufferInfo = ferro_memory_pool_allocate(&FerroPool, sizeof(ferro_fb_info_t))) == NULL) {
			Print(L"Error: Failed to allocate memory for Ferro framebuffer information structure.\n");
			return status;
		}
		Print(L"Info: Allocated space for Ferro framebuffer information structure.\n");

		FerroFramebufferInfo->base = (void*)GraphicsProtocol->Mode->FrameBufferBase;
		FerroFramebufferInfo->width = GraphicsProtocol->Mode->Info->HorizontalResolution;
		FerroFramebufferInfo->height = GraphicsProtocol->Mode->Info->VerticalResolution;

		switch (GraphicsProtocol->Mode->Info->PixelFormat) {
			case PixelRedGreenBlueReserved8BitPerColor: {
				FerroFramebufferInfo->pixel_bits = 32;
				FerroFramebufferInfo->red_mask = U32_BYTE_ZERO_MASK;
				FerroFramebufferInfo->green_mask = U32_BYTE_ONE_MASK;
				FerroFramebufferInfo->blue_mask = U32_BYTE_TWO_MASK;
				FerroFramebufferInfo->other_mask = U32_BYTE_THREE_MASK;
				Print(L"Info: Framebuffer uses RGB8.\n");
			} break;
			case PixelBlueGreenRedReserved8BitPerColor: {
				FerroFramebufferInfo->pixel_bits = 32;
				FerroFramebufferInfo->blue_mask = U32_BYTE_ZERO_MASK;
				FerroFramebufferInfo->green_mask = U32_BYTE_ONE_MASK;
				FerroFramebufferInfo->red_mask = U32_BYTE_TWO_MASK;
				FerroFramebufferInfo->other_mask = U32_BYTE_THREE_MASK;
				Print(L"Info: Framebuffer uses BGR8.\n");
			} break;
			case PixelBitMask: {
				// calculate the size of a pixel, according to the example in the UEFI spec
				FerroFramebufferInfo->pixel_bits = max(
					max(
						max(
							ferro_bits_in_use_u32(GraphicsProtocol->Mode->Info->PixelInformation.RedMask), 
							ferro_bits_in_use_u32(GraphicsProtocol->Mode->Info->PixelInformation.GreenMask)
						),
						ferro_bits_in_use_u32(GraphicsProtocol->Mode->Info->PixelInformation.BlueMask)
					),
					ferro_bits_in_use_u32(GraphicsProtocol->Mode->Info->PixelInformation.ReservedMask)
				);
				FerroFramebufferInfo->red_mask = GraphicsProtocol->Mode->Info->PixelInformation.RedMask;
				FerroFramebufferInfo->green_mask = GraphicsProtocol->Mode->Info->PixelInformation.GreenMask;
				FerroFramebufferInfo->blue_mask = GraphicsProtocol->Mode->Info->PixelInformation.BlueMask;
				FerroFramebufferInfo->other_mask = GraphicsProtocol->Mode->Info->PixelInformation.ReservedMask;
				Print(L"Info: Framebuffer uses custom masks (red=%x, green=%x, blue=%x).\n", FerroFramebufferInfo->red_mask, FerroFramebufferInfo->green_mask, FerroFramebufferInfo->blue_mask);
			} break;
			default: {
				Print(L"Warning: Invalid pixel format found!\n");
			} break;
		}

		// note that we assume we're dealing with a sane implementation that doesn't try to squeeze the most out of it's memory by packing partial pixels into the same byte
		// (e.g. if using 15bpp, it won't try to use *exactly* 15 bits, it'll pad to 16 instead and use 2 bytes per pixel)
		FerroFramebufferInfo->scan_line_size = round_up_div(FerroFramebufferInfo->pixel_bits, 8U) * GraphicsProtocol->Mode->Info->PixelsPerScanLine;

		Print(L"Info: Finished determining graphics framebuffer information.\n");
	}

	// read our config file
	if (ConfigFile != NULL) {
		if ((ConfigData = ferro_memory_pool_allocate(&FerroPool, ConfigDataSize)) == NULL) {
			Print(L"Warning: Failed to allocate memory for config data.\n");
		} else {
			// success
			Print(L"Info: Allocated memory for configuration file\n");

			// zero out the memory
			memset(ConfigData, 0, ConfigDataSize);

			// actually read the data
			if ((ConfigDataLength = FileRead(ConfigFile, ConfigDataLength, ConfigData)) == FileReadError) {
				Print(L"Warning: Failed to read config file.\n");
				ConfigData = NULL;
				ConfigDataLength = 0;
			} else {
				Print(L"Info: Read configuration file into memory\n");
			}
		}
	}

	// load in our ramdisk
	if (RamdiskFile != NULL) {
		if ((RamdiskAddress = ferro_memory_pool_allocate(&FerroPool, RamdiskSize)) == NULL) {
			Print(L"Warning: Failed to allocate memory for ramdisk contents.\n");
		} else {
			// success
			Print(L"Info: Allocated memory for ramdisk\n");

			// zero out the memory
			memset(RamdiskAddress, 0, RamdiskSize);

			// copy in the header
			memcpy(RamdiskAddress, &LocalRamdiskHeader, sizeof(ferro_ramdisk_header_t));

			ferro_ramdisk_header_t* RamdiskHeader = RamdiskAddress;

			// actually read the data
			if (FileRead(RamdiskFile, RamdiskSize - sizeof(ferro_ramdisk_header_t), RamdiskHeader->contents) == FileReadError) {
				Print(L"Warning: Failed to read ramdisk contents.\n");
				RamdiskAddress = NULL;
			} else {
				Print(L"Info: Read ramdisk into memory\n");
			}
		}
	}

	// allocate space for the kernel image info structure
	if ((KernelImageInfo = ferro_memory_pool_allocate(&FerroPool, sizeof(ferro_kernel_image_info_t))) == NULL) {
		Print(L"Error: Failed to allocate memory for kernel image information.\n");
		return status;
	}
	Print(L"Info: Allocated space for kernel image info structure\n");

	// set the number of loadable segments
	KernelImageInfo->segment_count = KernelLoadableSegmentCount;

	// allocate space for the segment info array
	if ((KernelImageInfo->segments = ferro_memory_pool_allocate(&FerroPool, sizeof(ferro_kernel_segment_t) * KernelImageInfo->segment_count)) == NULL) {
		Print(L"Error: Failed to allocate memory for kernel segment information table.\n");
		return status;
	}
	Print(L"Info: Allocated segment information array\n");

	// actually read in the segments
	for (ferro_elf_program_header_t* ProgramHeader = KernelProgramHeaders; (char*)ProgramHeader < KernelProgramHeadersEnd; ProgramHeader = (ferro_elf_program_header_t*)((char*)ProgramHeader + KernelHeader.program_header_entry_size)) {
		if (ProgramHeader->type == ferro_elf_program_header_type_loadable) {
			ferro_kernel_segment_t* Segment = &KernelImageInfo->segments[KernelSegmentIndex];

			Segment->page_count = (ProgramHeader->memory_size + 0xfff) / 0x1000;
			Segment->physical_address = (void*)(uintptr_t)ProgramHeader->physical_address;
			Segment->virtual_address = (void*)(uintptr_t)ProgramHeader->virtual_address;

			// allocate pages to store the segment
			if ((Segment->physical_address = BSAllocatePages(SystemTable->BootServices, AllocateAddress, EfiLoaderData, Segment->page_count, Segment->physical_address)) == NULL) {
				Print(L"Error: Failed to allocate memory for kernel segment.\n");
				return status;
			}

			// zero out the memory
			memset(Segment->physical_address, 0, Segment->page_count * 0x1000);

			// set the file position to read the segment
			if (FileSetPosition(KernelFile, ProgramHeader->offset) != EFI_SUCCESS) {
				Print(L"Error: Failed to set kernel file read offset for segment.\n");
				return status;
			}

			// actually read in the segment
			if (FileRead(KernelFile, ProgramHeader->file_size, Segment->physical_address) == FileReadError) {
				Print(L"Error: Failed to read kernel segment.\n");
				return status;
			}

			Print(L"Info: Read in section to physical address %lx and virtual address %lx.\n", Segment->physical_address, Segment->virtual_address);

			++KernelSegmentIndex;
		}
	}
	Print(L"Info: Loaded " UINTN_FORMAT " kernel segments\n", KernelSegmentIndex);

	// get the required size for the UEFI memory map
	Print(L"Info: Determining required size for memory map...\n");
	if (BSGetMemoryMap(SystemTable->BootServices, &MapSize, NULL, NULL, &DescriptorSize, NULL) != EFI_BUFFER_TOO_SMALL) {
		Print(L"Error: Failed to determine required memory map size.\n");
		return status;
	}
	MapEntryCount = MapSize / DescriptorSize;
	Print(L"Info: Initial UEFI memory map size: " UINTN_FORMAT " (count=" UINTN_FORMAT ")\n", MapSize, MapEntryCount);

	// account for additional descriptors that may need to be created for the allocation of the UEFI memory map as well as our own memory map for Ferro
	MapSize += 4 * DescriptorSize;

	// allocate the UEFI memory map
	if ((MemoryMap = BSAllocatePool(SystemTable->BootServices, EfiLoaderData, MapSize)) == NULL) {
		Print(L"Error: Failed to allocate memory to store UEFI memory map.\n");
		return status;
	}
	Print(L"Info: Allocated UEFI memory map\n");

	// allocate a memory map for Ferro
	// `+ 4` so we can map the memory map and initial pool as their own entries marked as "kernel reserved"
	// and we add the kernel segment count multiplied by two so we can map them as kernel reserved as well
	FerroMapSize = ((MapSize / DescriptorSize) + 4 + (KernelImageInfo->segment_count * 2)) * sizeof(ferro_memory_region_t);
	if ((FerroMemoryMap = BSAllocatePages(SystemTable->BootServices, AllocateAnyPages, EfiLoaderData, ROUND_UP_PAGE(FerroMapSize), NULL)) == NULL) {
		Print(L"Error: Failed to allocate memory to store Ferro memory map.\n");
		return status;
	}
	Print(L"Info: Allocated Ferro memory map\n");
	memset(FerroMemoryMap, 0, FerroMapSize);

	// can't call `Print` anymore after acquiring the memory map; it might allocate more memory and mess up the memory map
	Print(L"Info: Going to acquire final UEFI memory map (no more UEFI-based messages after this point)\n");
	// populate the UEFI memory map
	if ((status = BSGetMemoryMap(SystemTable->BootServices, &MapSize, MemoryMap, &MapKey, &DescriptorSize, &DescriptorVersion)) != EFI_SUCCESS) {
		//Print(L"Error: Failed to populate UEFI memory map.\n");
		return status;
	}
	MapEntryCount = MapSize / DescriptorSize;
	//Print(L"Info: Final UEFI memory map size: " UINTN_FORMAT " (count=" UINTN_FORMAT ")\n", MapSize, MapEntryCount);
	//Print(L"Info: Memory map key is " UINTN_FORMAT "\n", MapKey);

	// populate the Ferro memory map
	for (UINTN Index = 0; Index < MapEntryCount; ++Index) {
		EFI_MEMORY_DESCRIPTOR* Descriptor = (EFI_MEMORY_DESCRIPTOR*)((uintptr_t)MemoryMap + (Index * DescriptorSize));
		ferro_memory_region_t* FerroRegion = &FerroMemoryMap[Index];

		FerroRegion->type = uefi_to_ferro_memory_region_type(Descriptor->Type);
		FerroRegion->physical_start = Descriptor->PhysicalStart;
		FerroRegion->virtual_start = 0;
		FerroRegion->page_count = Descriptor->NumberOfPages;
	}
	//Print(L"Info: Populated Ferro memory map\n");

	// override types for special addresses
	for (UINTN Index = 0; Index < KernelImageInfo->segment_count + 2; ++Index) {
		uintptr_t physical_address = 0;
		uintptr_t virtual_address = 0;
		size_t page_count = 0;

		if (Index == KernelImageInfo->segment_count) {
			physical_address = (uintptr_t)FerroMemoryMap;
			page_count = ROUND_UP_PAGE(FerroMapSize);
		} else if (Index == KernelImageInfo->segment_count + 1) {
			physical_address = (uintptr_t)FerroPool.base_address;
			page_count = ROUND_UP_PAGE(FerroPoolSize);
		} else {
			ferro_kernel_segment_t* Segment = &KernelImageInfo->segments[Index];
			physical_address = (uintptr_t)Segment->physical_address;
			page_count = Segment->page_count;
			virtual_address = (uintptr_t)Segment->virtual_address;
		}

		for (UINTN Index2 = 0; Index2 < MapEntryCount; ++Index2) {
			ferro_memory_region_t* FerroRegion = &FerroMemoryMap[Index2];

			if ((uintptr_t)physical_address != FerroRegion->physical_start && (uintptr_t)physical_address > FerroRegion->physical_start && (uintptr_t)physical_address < FerroRegion->physical_start + (FerroRegion->page_count * 0x1000)) {
				ferro_memory_region_t* NewFerroRegion = &FerroMemoryMap[Index2 + 1];
				for (UINTN Index3 = MapEntryCount; Index3 > Index2 + 1; --Index3) {
					memcpy(&FerroMemoryMap[Index3], &FerroMemoryMap[Index3 - 1], sizeof(ferro_memory_region_t));
				}
				++MapEntryCount;
				NewFerroRegion->physical_start = (uintptr_t)physical_address;
				NewFerroRegion->page_count = ROUND_UP_PAGE(NewFerroRegion->physical_start - FerroRegion->physical_start);
				NewFerroRegion->type = FerroRegion->type;
				FerroRegion->page_count -= NewFerroRegion->page_count;
				FerroRegion = NewFerroRegion;
				++Index2;
			}

			if ((uintptr_t)physical_address == FerroRegion->physical_start) {
				if (FerroRegion->page_count > page_count) {
					// we have to create a new entry for the remaining memory
					ferro_memory_region_t* NewFerroRegion = &FerroMemoryMap[Index2 + 1];
					for (UINTN Index3 = MapEntryCount; Index3 > Index2 + 1; --Index3) {
						memcpy(&FerroMemoryMap[Index3], &FerroMemoryMap[Index3 - 1], sizeof(ferro_memory_region_t));
					}
					++MapEntryCount;
					NewFerroRegion->page_count = FerroRegion->page_count - page_count;
					NewFerroRegion->physical_start = FerroRegion->physical_start + (FerroRegion->page_count * 0x1000);
					NewFerroRegion->type = FerroRegion->type;
				}
				FerroRegion->type = ferro_memory_region_type_kernel_reserved;
				FerroRegion->virtual_start = virtual_address;
				break;
			}
		}
	}
	//Print(L"Info: Added kernel segment information to Ferro memory map\n");

	// allocate the boot data information array
	if ((FerroBootData = ferro_memory_pool_allocate(&FerroPool, sizeof(ferro_boot_data_info_t) * FerroBootDataCount)) == NULL) {
		//Print(L"Error: Failed to allocate memory for boot data information array.\n");
		return status;
	}
	//Print(L"Info: Allocated space for boot data information array.\n");

	// populate the boot data information array
	{
		size_t Index = 0;
		ferro_boot_data_info_t* info = NULL;

		info = &FerroBootData[Index++];
		info->physical_address = KernelImageInfo;
		info->size = sizeof(ferro_kernel_image_info_t);
		info->virtual_address = NULL;
		info->type = ferro_boot_data_type_kernel_image_info;

		info = &FerroBootData[Index++];
		info->physical_address = KernelImageInfo->segments;
		info->size = KernelImageInfo->segment_count * sizeof(ferro_kernel_segment_t);
		info->virtual_address = NULL;
		info->type = ferro_boot_data_type_kernel_segment_info_table;

		info = &FerroBootData[Index++];
		info->physical_address = FerroMemoryMap;
		info->size = FerroMapSize;
		info->virtual_address = NULL;
		info->type = ferro_boot_data_type_memory_map;

		info = &FerroBootData[Index++];
		info->physical_address = FerroPool.base_address;
		info->size = FerroPool.page_count * 0x1000;
		info->virtual_address = NULL;
		info->type = ferro_boot_data_type_initial_pool;

		if (GraphicsProtocol != NULL) {
			info = &FerroBootData[Index++];
			info->physical_address = FerroFramebufferInfo;
			info->size = sizeof(ferro_fb_info_t);
			info->virtual_address = NULL;
			info->type = ferro_boot_data_type_framebuffer_info;
		}

		if (ConfigData != NULL) {
			info = &FerroBootData[Index++];
			info->physical_address = ConfigData;
			info->size = ConfigDataSize;
			info->virtual_address = NULL;
			info->type = ferro_boot_data_type_config;
		}

		if (RamdiskAddress != NULL) {
			info = &FerroBootData[Index++];
			info->physical_address = RamdiskAddress;
			info->size = RamdiskSize;
			info->virtual_address = NULL;
			info->type = ferro_boot_data_type_ramdisk;
		}
	};

	// almost there: call ExitBootServices to prepare for kernel handoff
	//Print(L"Info: Going to exit boot services\n");
	if ((status = BSExitBootServices(SystemTable->BootServices, ImageHandle, MapKey)) != EFI_SUCCESS) {
		//Print(L"Error: Failed to terminate boot services (status=" EFI_STATUS_FORMAT ").\n", status);
		return status;
	}

	// finally, jump into our kernel
	((ferro_entry_t)KernelHeader.entry)(FerroPool.base_address, FerroPool.page_count, FerroBootData, FerroBootDataCount);

	// if we get here, we failed to load the kernel
	return EFI_LOAD_ERROR;
};
