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
 * Definitions for Ferro ramdisks.
 */

#ifndef _FERRO_CORE_RAMDISK_H_
#define _FERRO_CORE_RAMDISK_H_

#include <stdint.h>
#include <stddef.h>

#include <ferro/base.h>

FERRO_DECLARATIONS_BEGIN;

/**
 * @addtogroup Ramdisk
 *
 * Ramdisk definitions.
 *
 * @{
 */

FERRO_OPTIONS(uint32_t, ferro_ramdisk_directory_entry_flags) {
	/**
	 * Indicates that the directory entry is actually a directory itself (or rather, a subdirectory).
	 *
	 * If this flag is set, then ferro_ramdisk_directory_entry#contents_offset is an *index* into the directory section of the ramdisk.
	 * Note that in this case it is an *index*, not an offset. That is, a value of 2 means an offset of `2 * sizeof(ferro_ramdisk_directory_entry_t)` into the section.
	 *
	 * Otherwise, if this flag is not set, then it is an *offset* into the data section of the ramdisk.
	 * Note that in this case it is an *offset*, not an index. That is, a value of 2 means an offset of `2` into the section.
	 */
	ferro_ramdisk_directory_entry_flag_is_directory = 1 << 0,
};

FERRO_PACKED_STRUCT(ferro_ramdisk_directory_entry) {
	/**
	 * The index of the parent directory's entry in the directory section.
	 */
	uint64_t parent_index;

	/**
	 * An offset into the string table where the name of this entry is found.
	 */
	uint64_t name_offset;

	/**
	 * An offset relative to the entry's contents section where the contents of the entry can be found.
	 *
	 * Which section this is an offset into depends on #flags. See #flags for more details.
	 */
	uint64_t contents_offset;

	/**
	 * The size of the entry's contents.
	 *
	 * For files, this is the number of bytes in the file.
	 * For directories, this is the number of entries in the directory.
	 */
	uint64_t size;

	/**
	 * Flags describing the entry.
	 */
	ferro_ramdisk_directory_entry_flags_t flags;

	/**
	 * @todo This would be a perfect place to put a CRC32 for the data.
	 *       For files, that means computing the CRC32 of the file data (with padding of 0's at the end, if necessary).
	 *       For directories, that means computing the CRC32 of the directory entry list (with the CRC32's of those entries already filled in).
	 */
	uint32_t reserved;
};

FERRO_ENUM(uint16_t, ferro_ramdisk_section_type) {
	/**
	 * A section containing an array of null-terminated  strings, mainly used for string de-duplication among directory entry names.
	 */
	ferro_ramdisk_section_type_string_table,

	/**
	 * A section containing directory entry arrays describing the various directories contained by the ramdisk.
	 *
	 * This section always contains at least one entry at offset 0: the root directory.
	 * ferro_ramdisk_directory_entry#name_offset and ferro_ramdisk_directory_entry#parent_index are always UINT64_MAX for this entry.
	 */
	ferro_ramdisk_section_type_directories,

	/**
	 * A section containing raw binary data, mostly used for file contents.
	 */
	ferro_ramdisk_section_type_data,
};

FERRO_PACKED_STRUCT(ferro_ramdisk_section_header) {
	ferro_ramdisk_section_type_t type;
	uint16_t reserved1;
	uint32_t reserved2;

	/**
	 * The offset of this section from the end of ferro_ramdisk#section_headers.
	 */
	uint64_t offset;

	/**
	 * The total length (in bytes) of this section.
	 */
	uint64_t length;
};

FERRO_STRUCT(ferro_ramdisk_header) {
	/**
	 * The total size of the ramdisk contents. Does NOT include the size of this header (but it DOES include the size of the section count and section headers).
	 */
	uint64_t ramdisk_size;
};

/**
 * Ramdisks always contain at least one section: a directory entry array.
 */
FERRO_PACKED_STRUCT(ferro_ramdisk) {
	ferro_ramdisk_header_t header;

	uint64_t section_count;

	ferro_ramdisk_section_header_t section_headers[];
};

/**
 * Initializes the ramdisk subsystem.
 */
void ferro_ramdisk_init(ferro_ramdisk_t* ramdisk, void* phys, size_t ramdisk_size);

void ferro_ramdisk_get_data(ferro_ramdisk_t** out_ramdisk, void** out_phys, size_t* out_size);

/**
 * @}
 */

FERRO_DECLARATIONS_END;

#endif // _FERRO_CORE_RAMDISK_H_
