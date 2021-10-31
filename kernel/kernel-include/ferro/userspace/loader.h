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
 * A static binary loader for userspace code.
 */

#ifndef _FERRO_USERSPACE_LOADER_H_
#define _FERRO_USERSPACE_LOADER_H_

#include <ferro/base.h>
#include <ferro/error.h>
#include <ferro/core/vfs.h>
#include <ferro/elf.h>
#include <ferro/core/paging.h>

FERRO_DECLARATIONS_BEGIN;

FERRO_OPTIONS(uint64_t, fuloader_loaded_segment_flags) {
	/**
	 * Indicates that this segment is executable.
	 */
	fuloader_loaded_segment_flag_executable = 1ULL << 0,

	/**
	 * Indicates that this segment is not a part of the loaded binary but instead belongs to the binary's interpreter.
	 */
	fuloader_loaded_segment_flag_interpreter = 1ULL << 1,
};

FERRO_STRUCT(fuloader_loaded_segment_info) {
	fuloader_loaded_segment_flags_t flags;
	void* address;
	size_t size;
};

FERRO_STRUCT(fuloader_info) {
	fpage_space_t* space;

	void* entry_address;
	void* interpreter_entry_address;

	size_t loaded_segment_count;
	fuloader_loaded_segment_info_t loaded_segments[];
};

/**
 * Loads the ELF binary at pointed to by the given file descriptor into the current address space.
 *
 * This function only performs static loading. If a dynamic binary (one that uses dynamic libraries) is found, it will instead load the binary's interpreter
 * and return the necessary information in the ::fuloader_info structure.
 *
 * @param file_descriptor A VFS file descriptor pointing to the ELF binary to load.
 * @param space           A virtual address space in which to load the binary.
 * @param out_info        A pointer in which to store a pointer to the loaded binary's information structure.
 *
 * @retval ferr_ok               The binary and, if applicable, its interpreter have been loaded into memory and @p out_info now contains a pointer to the information structure for them.
 * @retval ferr_invalid_argument One or more of: 1) @p file_descriptor was not a valid VFS file descriptor, 2) @p out_info was `NULL`, 3) @p file_descriptor did not point to valid ELF executable, 4) the interpreter of the binary was not a valid ELF executable.
 * @retval ferr_temporary_outage There were insufficient resources available to load the binary and, if applicable, its interpreter.
 * @retval ferr_no_such_resource Can only occur when the binary has an interpreter and the binary's interpreter could not be found.
 * @retval ferr_forbidden        One or more of: 1) reading the binary's contents was not allowed, 2) reading the interpreter's contents was not allowed.
 */
FERRO_WUR ferr_t fuloader_load_file(fvfs_descriptor_t* file_descriptor, fpage_space_t* space, fuloader_info_t** out_info);

/**
 * Unloads the binary described by the given information structure and frees all resources held by it (including the information structure itself).
 *
 * @param info A pointer to the information structure describing the binary to unload.
 *
 * @note If the binary is a dynamic binary, it and any of its linked dynamic libraries are NOT unloaded by this call.
 *       Only the interpreter would be freed in that case. The binary and any of its linked dynamic libraries
 *       must be unloaded by the caller before calling this function (possibly by communicating with the interpreter somehow).
 *
 * @retval ferr_ok               The binary has been unloaded and all of its associated resources have been freed. @p info is now a dangling pointer.
 * @retval ferr_invalid_argument @p info was an invalid pointer (e.g. `NULL` or pointed to an invalid structure).
 */
FERRO_WUR ferr_t fuloader_unload_file(fuloader_info_t* info);

FERRO_DECLARATIONS_END;

#endif // _FERRO_USERSPACE_LOADER_H_
