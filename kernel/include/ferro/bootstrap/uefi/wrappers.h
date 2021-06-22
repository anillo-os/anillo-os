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

#ifndef _FERRO_BOOTSTRAP_UEFI_WRAPPERS_H_
#define _FERRO_BOOTSTRAP_UEFI_WRAPPERS_H_

#include <stdint.h>

#include <efi.h>

VOID* BSHandleProtocol(EFI_BOOT_SERVICES* This, EFI_HANDLE Handle, EFI_GUID* Guid);
VOID* BSAllocatePages(EFI_BOOT_SERVICES* This, EFI_ALLOCATE_TYPE Type, EFI_MEMORY_TYPE Label, UINTN NumberOfPages, VOID* TargetAddress);
EFI_STATUS BSGetMemoryMap(EFI_BOOT_SERVICES* This, UINTN* MemoryMapSize, EFI_MEMORY_DESCRIPTOR* MemoryMap, UINTN* MapKey, UINTN* DescriptorSize, UINT32* DescriptorVersion);
VOID* BSAllocatePool(EFI_BOOT_SERVICES* This, EFI_MEMORY_TYPE Label, UINTN Size);
EFI_STATUS BSExitBootServices(EFI_BOOT_SERVICES* This, EFI_HANDLE ImageHandle, UINTN MapKey);
VOID* BSLocateProtocol(EFI_BOOT_SERVICES* This, EFI_GUID Guid);

EFI_FILE* FSOpenVolume(EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* This);

EFI_FILE* FileOpen(EFI_FILE* This, CHAR16* FileName, UINT64 Mode, UINT64 Attributes);
UINTN FileRead(EFI_FILE* This, UINTN BufferSize, VOID* Buffer);
EFI_STATUS FileSetPosition(EFI_FILE* This, UINT64 Position);

#if __x86_64__ || __aarch64__
	#define FileReadError UINT64_MAX
#else
	#define FileReadError UINT32_MAX
#endif

#endif // _FERRO_BOOTSTRAP_UEFI_WRAPPERS_H_
