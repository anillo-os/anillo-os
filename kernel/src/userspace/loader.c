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

#include <ferro/userspace/loader.h>
#include <ferro/core/mempool.h>
#include <ferro/core/paging.h>
#include <libsimple/libsimple.h>

#define LOADING_ELF 0

#if LOADING_ELF
#include <libelf/libelf.h>
#else
#include <libmacho/libmacho.h>
#endif

// how many consecutive "ferr_temporary_outage"s we can receive before giving up
#define OUTAGE_LIMIT 4

static ferr_t fuloader_read_file(fvfs_descriptor_t* file_descriptor, size_t offset, void* buffer, size_t read_count_exact) {
	ferr_t status = ferr_ok;
	char* buffer_offset = buffer;
	size_t total_read_count = 0;
	size_t current_read_count = 0;
	size_t outages = 0;

	while (total_read_count < read_count_exact) {
		status = fvfs_read(file_descriptor, offset + total_read_count, buffer_offset, read_count_exact - total_read_count, &current_read_count);
		if (status != ferr_ok) {
			switch (status) {
				case ferr_permanent_outage:
				case ferr_unsupported:
					status = ferr_invalid_argument;
					break;
				case ferr_temporary_outage:
					if (outages < OUTAGE_LIMIT) {
						// try again
						status = ferr_ok;
						++outages;
						continue;
					}
					// otherwise, we've reached the attempt limit on temporary outages.
					// stop here and report failure.
					break;
				default:
					break;
			}

			break;
		} else {
			// this call succeeded, so any previous streak of outages has been broken.
			outages = 0;
		}

		total_read_count += current_read_count;
		buffer_offset += current_read_count;
		current_read_count = 0;
	}

	return status;
};

#if LOADING_ELF

static bool validate_header(ferro_elf_header_t* header) {
	if (
		(header->magic != FERRO_ELF_MAGIC) ||
		(header->bits != ferro_elf_bits_64) ||
#if FERRO_ENDIANNESS == FERRO_ENDIANNESS_BIG
		(header->endianness != ferro_elf_endianness_big) ||
#elif FERRO_ENDIANNESS == FERRO_ENDIANNESS_LITTLE
		(header->endianness != ferro_elf_endianness_little) ||
#else
	#error Invalid endianness (see src/userspace/loader.c)
#endif
		(header->identifier_version != FERRO_ELF_IDENTIFIER_VERSION) ||
		(header->abi != ferro_elf_abi_sysv) ||
		(header->abi_version != 0) ||
#if FERRO_ARCH == FERRO_ARCH_x86_64
		(header->machine != ferro_elf_machine_amd64) ||
#elif FERRO_ARCH == FERRO_ARCH_aarch64
		(header->machine != ferro_elf_machine_arm64) ||
#endif
		(header->format_version != FERRO_ELF_FORMAT_VERSION)
	) {
		return false;
	}

	return true;
};

ferr_t fuloader_load_file(fvfs_descriptor_t* file_descriptor, fpage_space_t* space, fuloader_info_t** out_info) {
	ferr_t status = ferr_ok;
	ferro_elf_header_t header;
	ferro_elf_header_t interpreter_header;
	ferro_elf_program_header_t* program_headers = NULL;
	size_t loadable_segment_count = 0;
	char* program_headers_end = NULL;
	fuloader_info_t* info = NULL;
	fpage_space_t* previous_space = fpage_space_current();
	fvfs_descriptor_t* interpreter_descriptor = NULL;

	ferro_elf_header_t* header_to_load = &header;
	fvfs_descriptor_t* file_to_load = file_descriptor;

	FERRO_WUR_IGNORE(fpage_space_swap(space));

	// read the main ELF header
	status = fuloader_read_file(file_descriptor, 0, &header, sizeof(header));
	if (status != ferr_ok) {
		goto out;
	}

	// perform some sanity checks
	if (!validate_header(&header)) {
		status = ferr_invalid_argument;
		goto out;
	}

	if (header.type == ferro_elf_type_shared_object) {
		// this should be a dynamic executable;
		// let's look for its interpreter
		char interpreter_path[256];
		size_t interpreter_path_length = SIZE_MAX;

		for (size_t i = 0; i < header.program_header_entry_count; ++i) {
			ferro_elf_program_header_t program_header;

			status = fuloader_read_file(file_descriptor, header.program_header_table_offset + (i * header.program_header_entry_size), &program_header, sizeof(program_header));
			if (status != ferr_ok) {
				goto out;
			}

			if (program_header.type != ferro_elf_program_header_type_interpreter_information) {
				continue;
			}

			// the file size already includes the null terminator, so subtract 1
			if (program_header.file_size - 1 > sizeof(interpreter_path)) {
				// if we don't have enough space, consider it invalid
				status = ferr_invalid_argument;
				goto out;
			}

			status = fuloader_read_file(file_descriptor, program_header.offset, interpreter_path, program_header.file_size - 1);
			if (status != ferr_ok) {
				goto out;
			}

			interpreter_path_length = program_header.file_size - 1;
			break;
		}

		if (interpreter_path_length == SIZE_MAX) {
			// if we didn't find an interpreter path, this is not a valid dynamic executable
			status = ferr_invalid_argument;
			goto out;
		}

		// now try to open a file descriptor for the interpreter
		status = fvfs_open_n(interpreter_path, interpreter_path_length, fvfs_descriptor_flag_read | fvfs_descriptor_flags_execute, &interpreter_descriptor);
		if (status != ferr_ok) {
			goto out;
		}

		// read the main ELF header
		status = fuloader_read_file(interpreter_descriptor, 0, &interpreter_header, sizeof(interpreter_header));
		if (status != ferr_ok) {
			goto out;
		}

		if (!validate_header(&interpreter_header)) {
			status = ferr_invalid_argument;
			goto out;
		}

		// if the interpreter is not a static executable, it's not a valid interpreter
		if (interpreter_header.type != ferro_elf_type_executable) {
			status = ferr_invalid_argument;
			goto out;
		}

		header_to_load = &interpreter_header;
		file_to_load = interpreter_descriptor;
	} else if (header.type != ferro_elf_type_executable) {
		// if it's not a dynamic executable AND not a static one,
		// it's invalid
		status = ferr_invalid_argument;
		goto out;
	}

	// allocate space for the program headers
	if (fmempool_allocate(header_to_load->program_header_entry_size * header_to_load->program_header_entry_count, NULL, (void*)&program_headers) != ferr_ok) {
		status = ferr_temporary_outage;
		goto out;
	}

	program_headers_end = (char*)program_headers + (header_to_load->program_header_entry_size * header_to_load->program_header_entry_count);

	// read the program headers
	status = fuloader_read_file(file_to_load, header_to_load->program_header_table_offset, program_headers, header_to_load->program_header_entry_size * header_to_load->program_header_entry_count);
	if (status != ferr_ok) {
		goto out;
	}

	// determine how many loadable segments we have
	for (ferro_elf_program_header_t* program_header = program_headers; (char*)program_header < program_headers_end; program_header = (ferro_elf_program_header_t*)((char*)program_header + header_to_load->program_header_entry_size)) {
		if (program_header->type != ferro_elf_program_header_type_loadable) {
			continue;
		}

		++loadable_segment_count;
	}

	// allocate an information structure
	if (fmempool_allocate(sizeof(fuloader_info_t) + (sizeof(fuloader_loaded_segment_info_t) * loadable_segment_count), NULL, (void*)&info) != ferr_ok) {
		status = ferr_temporary_outage;
		goto out;
	}

	info->entry_address = file_to_load == file_descriptor ? (void*)header_to_load->entry : NULL;
	info->interpreter_entry_address = file_to_load == file_descriptor ? NULL :  (void*)header_to_load->entry;

	info->loaded_segment_count = 0;

	// load the segments
	for (ferro_elf_program_header_t* program_header = program_headers; (char*)program_header < program_headers_end; program_header = (ferro_elf_program_header_t*)((char*)program_header + header_to_load->program_header_entry_size)) {
		if (program_header->type != ferro_elf_program_header_type_loadable) {
			continue;
		}

		uintptr_t page_start = fpage_round_down_page(program_header->virtual_address);

		// allocate space for the segment
		// TODO: only mark it as executable if the segment is executable
		if (fpage_space_allocate_fixed(space, fpage_round_up_to_page_count((program_header->virtual_address + program_header->memory_size) - page_start), (void*)page_start, fpage_flag_unprivileged) != ferr_ok) {
			status = ferr_temporary_outage;
			goto out;
		}

		// mark the segment as loaded already (for the purpose of tracking which ones have been allocated, in case of failure)
		++info->loaded_segment_count;
		info->loaded_segments[info->loaded_segment_count - 1].address = (void*)program_header->virtual_address;
		info->loaded_segments[info->loaded_segment_count - 1].flags = (program_header->flags & ferro_elf_program_header_flag_execute) ? fuloader_loaded_segment_flag_executable : 0;
		info->loaded_segments[info->loaded_segment_count - 1].size = program_header->memory_size;

		if (file_to_load == interpreter_descriptor) {
			info->loaded_segments[info->loaded_segment_count - 1].flags |= fuloader_loaded_segment_flag_interpreter;
		}

		// read it in from the file
		status = fuloader_read_file(file_to_load, program_header->offset, (void*)program_header->virtual_address, program_header->file_size);
		if (status != ferr_ok) {
			goto out;
		}

		// zero out uninitialized memory
		simple_memset((char*)program_header->virtual_address + program_header->file_size, 0, program_header->memory_size - program_header->file_size);
	}

out:
	if (program_headers != NULL) {
		FERRO_WUR_IGNORE(fmempool_free(program_headers));
	}
	if (interpreter_descriptor != NULL) {
		fvfs_release(interpreter_descriptor);
	}
	if (status != ferr_ok) {
		if (info) {
			for (size_t i = 0; i < info->loaded_segment_count; ++i) {
				FERRO_WUR_IGNORE(fpage_space_free(space, (void*)fpage_round_down_page((uintptr_t)info->loaded_segments[i].address), fpage_round_up_to_page_count(info->loaded_segments[i].size)));
			}

			FERRO_WUR_IGNORE(fmempool_free(info));
		}
	} else {
		info->space = space;
		*out_info = info;
	}
	FERRO_WUR_IGNORE(fpage_space_swap(previous_space));
	return status;
};

#else

static bool validate_header(macho_header_t* header) {
	if (
		header->magic != MACHO_MAGIC_64 ||
#if FERRO_ARCH == FERRO_ARCH_x86_64
		header->cpu_type != macho_cpu_type_x86_64 ||
		header->cpu_subtype != macho_cpu_subtype_x86_64_all ||
#elif FERRO_ARCH == FERRO_ARCH_aarch64
		header->cpu_subtype != macho_cpu_type_aarch64 ||
#endif
		false
	) {
		return false;
	}
	return true;
};

ferr_t fuloader_load_file(fvfs_descriptor_t* file_descriptor, fpage_space_t* space, fuloader_info_t** out_info) {
	ferr_t status = ferr_ok;
	macho_header_t header;
	macho_header_t dynamic_linker_header;
	size_t loadable_segment_count = 0;
	fuloader_info_t* info = NULL;
	fpage_space_t* previous_space = fpage_space_current();
	fvfs_descriptor_t* dynamic_linker_descriptor = NULL;

	macho_header_t* header_to_load = &header;
	fvfs_descriptor_t* file_to_load = file_descriptor;

	macho_load_command_t load_command = {0};
	size_t file_offset = 0;

	void* entry_address = NULL;

	if (!out_info) {
		return ferr_invalid_argument;
	}

	FERRO_WUR_IGNORE(fpage_space_swap(space));

	// read the main Mach-O header
	status = fuloader_read_file(file_descriptor, 0, &header, sizeof(header));
	if (status != ferr_ok) {
		goto out;
	}

	// perform some sanity checks
	if (!validate_header(&header)) {
		status = ferr_invalid_argument;
		goto out;
	}

	// if it's not an executable, we can't execute it
	if (header.file_type != macho_file_type_exectuable) {
		status = ferr_invalid_argument;
		goto out;
	}

	if ((header.flags & macho_header_flag_dynamically_linked) != 0) {
		// this is a dynamically linked executable, meaning we'll need to load the dynamic linker instead
		// (and it will, in turn, load the executable)

		char dynamic_linker_path[256];
		size_t dynamic_linker_path_length = SIZE_MAX;

		// let's look for the dynamic linker command
		file_offset = sizeof(header);
		for (size_t i = 0; i < header.command_count; (++i), (file_offset += load_command.size)) {
			uint32_t name_offset = 0;
			size_t name_length = 0;

			status = fuloader_read_file(file_descriptor, file_offset, &load_command, sizeof(load_command));
			if (status != ferr_ok) {
				goto out;
			}

			if (load_command.type != macho_load_command_type_load_dynamic_linker) {
				continue;
			}

			status = fuloader_read_file(file_descriptor, file_offset + sizeof(load_command), &name_offset, sizeof(name_offset));
			if (status != ferr_ok) {
				goto out;
			}

			name_length = load_command.size - name_offset;
			if (name_length > sizeof(dynamic_linker_path)) {
				status = ferr_invalid_argument;
				goto out;
			}

			status = fuloader_read_file(file_descriptor, file_offset + name_offset, &dynamic_linker_path[0], name_length);
			if (status != ferr_ok) {
				goto out;
			}

			// the name can include zero padding at the end, so find the real length
			name_length = simple_strnlen(dynamic_linker_path, name_length);

			dynamic_linker_path_length = name_length;
			break;
		}

		if (dynamic_linker_path_length == SIZE_MAX) {
			// if we didn't find a dynamic linker path, this is not a valid dynamic executable
			status = ferr_invalid_argument;
			goto out;
		}

		// now try to open a file descriptor for the dynamic linker
		status = fvfs_open_n(dynamic_linker_path, dynamic_linker_path_length, fvfs_descriptor_flag_read | fvfs_descriptor_flags_execute, &dynamic_linker_descriptor);
		if (status != ferr_ok) {
			goto out;
		}

		// read the main Mach-O header
		status = fuloader_read_file(dynamic_linker_descriptor, 0, &dynamic_linker_header, sizeof(dynamic_linker_header));
		if (status != ferr_ok) {
			goto out;
		}

		if (!validate_header(&dynamic_linker_header)) {
			status = ferr_invalid_argument;
			goto out;
		}

		// if the dynamic linker is not a dynamic linker, it's not a valid dynamic linker (duh)
		if (dynamic_linker_header.file_type != macho_file_type_dynamic_linker) {
			status = ferr_invalid_argument;
			goto out;
		}

		header_to_load = &dynamic_linker_header;
		file_to_load = dynamic_linker_descriptor;
	}

	// determine how many loadable segments we have and what the entry address is
	file_offset = sizeof(*header_to_load);
	for (size_t i = 0; i < header_to_load->command_count; (++i), (file_offset += load_command.size)) {
		status = fuloader_read_file(file_to_load, file_offset, &load_command, sizeof(load_command));
		if (status != ferr_ok) {
			goto out;
		}

		if (load_command.type == macho_load_command_type_segment_64) {
			++loadable_segment_count;
		} else if (load_command.type == macho_load_command_type_unix_thread) {
			// dynamically linked executables are supposed to use the "main" load command rather than "unix thread".
			// besides, how did we even get here? dynamic executables are supposed to load their dynamic linker instead.
			if (file_to_load == file_descriptor && (header_to_load->flags & macho_header_flag_dynamically_linked) != 0) {
				status = ferr_invalid_argument;
				goto out;
			}

#if FERRO_ARCH == FERRO_ARCH_x86_64
			// 4 * sizeof(uint32_t) for the command type, command size, flavor, and count fields
			// 16 * sizeof(uint64_t) because `rip` is the 16th entry in the array
			status = fuloader_read_file(file_to_load, file_offset + (4 * sizeof(uint32_t)) + (16 * sizeof(uint64_t)), &entry_address, sizeof(entry_address));
#elif FERRO_ARCH == FERRO_ARCH_aarch64
			status = fuloader_read_file(file_to_load, file_offset + (4 * sizeof(uint32_t)) + (32 * sizeof(uint64_t)), &entry_address, sizeof(entry_address));
#else
			#error Unimplemented on this architecture
#endif
			if (status != ferr_ok) {
				goto out;
			}
		}
	}

	// allocate an information structure
	if (fmempool_allocate(sizeof(fuloader_info_t) + (sizeof(fuloader_loaded_segment_info_t) * loadable_segment_count), NULL, (void*)&info) != ferr_ok) {
		status = ferr_temporary_outage;
		goto out;
	}

	if (file_to_load == file_descriptor) {
		info->entry_address = (void*)entry_address;
	} else {
		info->interpreter_entry_address = (void*)entry_address;
	}

	info->loaded_segment_count = 0;

	// load the segments
	file_offset = sizeof(*header_to_load);
	for (size_t i = 0; i < header_to_load->command_count; (++i), (file_offset += load_command.size)) {
		macho_load_command_segment_64_t segment_64_load_command = {0};
		uintptr_t page_start = 0;

		status = fuloader_read_file(file_to_load, file_offset, &load_command, sizeof(load_command));
		if (status != ferr_ok) {
			goto out;
		}

		if (load_command.type != macho_load_command_type_segment_64) {
			continue;
		}

		status = fuloader_read_file(file_to_load, file_offset, &segment_64_load_command, sizeof(segment_64_load_command));
		if (status != ferr_ok) {
			goto out;
		}

		if (segment_64_load_command.initial_memory_protection == 0 && segment_64_load_command.maximum_memory_protection == 0) {
			// this is a reserved-as-invalid segment, most likely __PAGEZERO.
			// just skip it.
			// XXX: this is wrong; we should actually reserve it in the memory manager so no memory is ever allocated in this region.
			continue;
		}

		page_start = fpage_round_down_page(segment_64_load_command.memory_address);

		// allocate space for the segment
		// TODO: only mark it as executable if the segment is executable
		if (fpage_space_allocate_fixed(space, fpage_round_up_to_page_count((segment_64_load_command.memory_address + segment_64_load_command.memory_size) - page_start), (void*)page_start, fpage_flag_unprivileged) != ferr_ok) {
			status = ferr_temporary_outage;
			goto out;
		}

		// mark the segment as loaded already (for the purpose of tracking which ones have been allocated, in case of failure)
		++info->loaded_segment_count;
		info->loaded_segments[info->loaded_segment_count - 1].address = (void*)segment_64_load_command.memory_address;
		info->loaded_segments[info->loaded_segment_count - 1].flags = (segment_64_load_command.initial_memory_protection & macho_memory_protection_flag_execute) ? fuloader_loaded_segment_flag_executable : 0;
		info->loaded_segments[info->loaded_segment_count - 1].size = segment_64_load_command.memory_size;

		if (file_to_load == dynamic_linker_descriptor) {
			info->loaded_segments[info->loaded_segment_count - 1].flags |= fuloader_loaded_segment_flag_interpreter;
		}

		// read it in from the file
		status = fuloader_read_file(file_to_load, segment_64_load_command.file_offset, (void*)segment_64_load_command.memory_address, segment_64_load_command.file_size);
		if (status != ferr_ok) {
			goto out;
		}

		// zero out uninitialized memory
		simple_memset((char*)segment_64_load_command.memory_address + segment_64_load_command.file_size, 0, segment_64_load_command.memory_size - segment_64_load_command.file_size);
	}

out:
	if (status == ferr_ok) {
		info->space = space;
		*out_info = info;
	} else {
		if (info) {
			for (size_t i = 0; i < info->loaded_segment_count; ++i) {
				FERRO_WUR_IGNORE(fpage_space_free(space, (void*)fpage_round_down_page((uintptr_t)info->loaded_segments[i].address), fpage_round_up_to_page_count(info->loaded_segments[i].size)));
			}

			FERRO_WUR_IGNORE(fmempool_free(info));
		}
	}
	FERRO_WUR_IGNORE(fpage_space_swap(previous_space));
	return status;
};

#endif

ferr_t fuloader_unload_file(fuloader_info_t* info) {
	if (!info || !info->space) {
		return ferr_invalid_argument;
	}

	for (size_t i = 0; i < info->loaded_segment_count; ++i) {
		FERRO_WUR_IGNORE(fpage_space_free(info->space, (void*)fpage_round_down_page((uintptr_t)info->loaded_segments[i].address), fpage_round_up_to_page_count(info->loaded_segments[i].size)));
	}

	FERRO_WUR_IGNORE(fmempool_free(info));

	return ferr_ok;
};
