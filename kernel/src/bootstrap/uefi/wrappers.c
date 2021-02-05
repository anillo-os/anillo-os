#include <ferro/bootstrap/uefi/wrappers.h>

VOID* BSHandleProtocol(EFI_BOOT_SERVICES* This, EFI_HANDLE Handle, EFI_GUID* Guid) {
	void* Protocol = NULL;
	if (uefi_call_wrapper(This->HandleProtocol, 3, Handle, Guid, &Protocol) != EFI_SUCCESS) {
		return NULL;
	}
	return Protocol;
};

VOID* BSAllocatePages(EFI_BOOT_SERVICES* This, EFI_ALLOCATE_TYPE Type, EFI_MEMORY_TYPE Label, UINTN NumberOfPages, VOID* TargetAddress) {
	EFI_PHYSICAL_ADDRESS Address = (uintptr_t)TargetAddress;
	if (uefi_call_wrapper(This->AllocatePages, 4, Type, Label, NumberOfPages, &Address) != EFI_SUCCESS) {
		return NULL;
	}
	return (void*)Address;
};

EFI_STATUS BSGetMemoryMap(EFI_BOOT_SERVICES* This, UINTN* MemoryMapSize, EFI_MEMORY_DESCRIPTOR* MemoryMap, UINTN* MapKey, UINTN* DescriptorSize, UINT32* DescriptorVersion) {
	return uefi_call_wrapper(This->GetMemoryMap, 5, MemoryMapSize, MemoryMap, MapKey, DescriptorSize, DescriptorVersion);
};

VOID* BSAllocatePool(EFI_BOOT_SERVICES* This, EFI_MEMORY_TYPE Label, UINTN Size) {
	VOID* Address = NULL;
	if (uefi_call_wrapper(This->AllocatePool, 3, Label, Size, &Address) != EFI_SUCCESS) {
		return NULL;
	}
	return Address;
};

EFI_STATUS BSExitBootServices(EFI_BOOT_SERVICES* This, EFI_HANDLE ImageHandle, UINTN MapKey) {
	return uefi_call_wrapper(This->ExitBootServices, 2, ImageHandle, MapKey);
};

VOID* BSLocateProtocol(EFI_BOOT_SERVICES* This, EFI_GUID Guid) {
	VOID* Protocol = NULL;
	if (uefi_call_wrapper(This->LocateProtocol, 3, &Guid, NULL, &Protocol) != EFI_SUCCESS) {
		return NULL;
	}
	return Protocol;
};

EFI_FILE* FSOpenVolume(EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* This) {
	EFI_FILE* Root = NULL;
	if (uefi_call_wrapper(This->OpenVolume, 2, This, &Root) != EFI_SUCCESS) {
		return NULL;
	}
	return Root;
};

EFI_FILE* FileOpen(EFI_FILE* This, CHAR16* FileName, UINT64 Mode, UINT64 Attributes) {
	EFI_FILE* NewFile = NULL;
	if (uefi_call_wrapper(This->Open, 5, This, &NewFile, FileName, Mode, Attributes) != EFI_SUCCESS) {
		return NULL;
	}
	return NewFile;
};

UINTN FileRead(EFI_FILE* This, UINTN BufferSize, VOID* Buffer) {
	UINTN BytesRead = BufferSize >= FileReadError ? BufferSize - 1 : BufferSize;
	if (uefi_call_wrapper(This->Read, 3, This, &BytesRead, Buffer) != EFI_SUCCESS) {
		return FileReadError;
	}
	return BytesRead;
};

EFI_STATUS FileSetPosition(EFI_FILE* This, UINT64 Position) {
	return uefi_call_wrapper(This->SetPosition, 2, This, Position);
};
