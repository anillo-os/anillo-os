/*
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
#include <stddef.h>
#include <stdbool.h>
#include <limits.h>
#include <stdarg.h>

#include <ferro/base.h>
#include <ferro/bootstrap/uefi/definitions.h>

FERRO_DECLARATIONS_BEGIN;

//
// types
//

typedef long long int off_t;
typedef void FILE;

FERRO_STRUCT(fuefi_sysctl_bs_memory_map_info) {
	size_t map_size;
	size_t descriptor_size;
};

FERRO_STRUCT(fuefi_sysctl_bs_populate_memory_map) {
	size_t map_size;
	size_t descriptor_size;
	uint32_t descriptor_version;
	fuefi_memory_map_key_t map_key;
	fuefi_memory_descriptor_t* memory_map;
};

FERRO_STRUCT(fuefi_sysctl_wrappers_init) {
	fuefi_image_handle_t image_handle;
	fuefi_system_table_t* system_table;
};

//
// constants
//

#define PROT_NONE  0
#define PROT_READ  4
#define PROT_WRITE 2
#define PROT_EXEC  1

#define MAP_PRIVATE   1
#define MAP_FIXED     2
#define MAP_ANON 4

#define MAP_FAILED NULL

#define EOF (-1)

#define FUEFI_PRINTF(a, b) __attribute__((format(printf, a, b)))

enum {
	_SC_FB_AVAILABLE           =  1,
	_SC_FB_BASE,
	_SC_FB_WIDTH,
	_SC_FB_HEIGHT,
	_SC_FB_RED_MASK,
	_SC_FB_GREEN_MASK,
	_SC_FB_BLUE_MASK,
	_SC_FB_RESERVED_MASK,
	_SC_FB_BIT_COUNT,
	_SC_FB_PIXELS_PER_SCANLINE,
	_SC_IMAGE_BASE,
	_SC_ACPI_RSDP,
};

enum {
	SEEK_SET,
};

enum {
	// boot services
	CTL_BS,
	CTL_WRAPPERS,
};

enum {
	BS_MEMORY_MAP_INFO,
	BS_POPULATE_MEMORY_MAP,
	BS_EXIT_BOOT_SERVICES,
};

enum {
	WRAPPERS_INIT,
};

#define FUEFI_STATUS_FORMAT "%zx"

//
// global variables
//

extern int errno;
extern fuefi_status_t errstat;

//
// functions
//

void* malloc(size_t byte_size);
void free(void* memory);

void* mmap(void* address, size_t length, int protection, int flags, int fd, off_t offset);
int munmap(void* address, size_t length);

int putchar(int character);
int vprintf(const char* format, va_list args) FUEFI_PRINTF(1, 0);
int printf(const char* format, ...) FUEFI_PRINTF(1, 2);

FILE* fopen(const char* filename, const char* mode);
int fclose(FILE* file);
size_t fread(void* buffer, size_t element_size, size_t element_count, FILE* file);
int fseek(FILE* file, long long int offset, int origin);

void* memset(void* destination, int value, size_t count);
void* memcpy(void* destination, const void* source, size_t count);
size_t strlen(const char* string);

//
// most of our wrappers imitate a POSIX and/or C environment,
// but there's nothing specifically for graphics in those standards.
//
// so... let's make a sysconf instead!
//

long long sysconf(int name);

int sysctl(const int* name, unsigned int name_length, void* old_data, size_t* old_data_length, void* new_data, size_t new_data_length);

FERRO_DECLARATIONS_END;

#endif // _FERRO_BOOTSTRAP_UEFI_WRAPPERS_H_
