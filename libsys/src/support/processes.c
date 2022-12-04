/*
 * This file is part of Anillo OS
 * Copyright (C) 2022 Anillo OS Developers
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

#include <libsys/processes.private.h>
#include <libsys/abort.h>
#include <gen/libsyscall/syscall-wrappers.h>
#include <libsimple/general.h>
#include <libsys/mempool.h>
#include <libmacho/libmacho.h>
#include <libsys/pages.h>
#include <libsys/files.private.h>
#include <libvfs/libvfs.private.h>
#include <libspooky/proxy.private.h>
#include <libsys/channels.private.h>

static sys_proc_object_t* this_process = NULL;

static void sys_proc_destroy(sys_proc_t* object);

static const sys_object_class_t proc_class = {
	LIBSYS_OBJECT_CLASS_INTERFACE(NULL),
	.destroy = sys_proc_destroy,
};

LIBSYS_OBJECT_CLASS_GETTER(proc, proc_class);

static void sys_proc_destroy(sys_proc_t* object) {
	sys_proc_object_t* proc = (void*)object;

	if (proc->id != SYS_PROC_ID_INVALID && !proc->detached) {
		sys_abort_status(libsyscall_wrapper_process_kill(proc->handle));
	}

	sys_object_destroy(object);
};

ferr_t sys_proc_init(void) {
	ferr_t status = ferr_ok;

	status = sys_object_new(&proc_class, sizeof(sys_proc_object_t) - sizeof(sys_object_t), (void*)&this_process);
	if (status != ferr_ok) {
		goto out;
	}

	this_process->id = SYS_PROC_ID_INVALID;
	this_process->detached = true;

	status = libsyscall_wrapper_process_current(&this_process->handle);

	status = libsyscall_wrapper_process_id(this_process->handle, &this_process->id);
	if (status != ferr_ok) {
		goto out;
	}

out:
	if (status != ferr_ok) {
		if (this_process) {
			sys_release((void*)this_process);
			this_process = NULL;
		}
	}
	return status;
};

static bool validate_header(macho_header_t* header) {
	if (
		header->magic != MACHO_MAGIC_64 ||
#if FERRO_ARCH == FERRO_ARCH_x86_64
		header->cpu_type != macho_cpu_type_x86_64 ||
		header->cpu_subtype != macho_cpu_subtype_x86_64_all ||
#elif FERRO_ARCH == FERRO_ARCH_aarch64
		header->cpu_type != macho_cpu_type_aarch64 ||
#endif
		false
	) {
		return false;
	}
	return true;
};

LIBSYS_OPTIONS(uint64_t, sys_uloader_loaded_segment_flags) {
	/**
	 * Indicates that this segment is executable.
	 */
	sys_uloader_loaded_segment_flag_executable = 1ULL << 0,

	/**
	 * Indicates that this segment is not a part of the loaded binary but instead belongs to the binary's interpreter.
	 */
	sys_uloader_loaded_segment_flag_interpreter = 1ULL << 1,
};

LIBSYS_STRUCT(sys_uloader_loaded_segment_info) {
	sys_uloader_loaded_segment_flags_t flags;
	void* load_address;
	void* target_address;
	void* aligned_target_address;
	size_t size;
	size_t aligned_size;
};

LIBSYS_STRUCT(sys_uloader_info) {
	void* entry_address;
	void* interpreter_entry_address;

	size_t loaded_segment_count;
	sys_uloader_loaded_segment_info_t loaded_segments[];
};

static ferr_t sys_uloader_load_file(sys_file_t* file, sys_uloader_info_t** out_info) {
	ferr_t status = ferr_ok;
	macho_header_t header;
	macho_header_t dynamic_linker_header;
	size_t loadable_segment_count = 0;
	sys_uloader_info_t* info = NULL;
	sys_file_t* dynamic_linker_descriptor = NULL;

	macho_header_t* header_to_load = &header;
	sys_file_t* file_to_load = file;

	macho_load_command_t load_command = {0};
	size_t file_offset = 0;

	void* entry_address = NULL;

	if (!out_info) {
		return ferr_invalid_argument;
	}

	// read the main Mach-O header
	status = sys_file_read_retry(file, 0, sizeof(header), &header, NULL);
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

			status = sys_file_read_retry(file, file_offset, sizeof(load_command), &load_command, NULL);
			if (status != ferr_ok) {
				goto out;
			}

			if (load_command.type != macho_load_command_type_load_dynamic_linker) {
				continue;
			}

			status = sys_file_read_retry(file, file_offset + sizeof(load_command), sizeof(name_offset), &name_offset, NULL);
			if (status != ferr_ok) {
				goto out;
			}

			name_length = load_command.size - name_offset;
			if (name_length > sizeof(dynamic_linker_path)) {
				status = ferr_invalid_argument;
				goto out;
			}

			status = sys_file_read_retry(file, file_offset + name_offset, name_length, &dynamic_linker_path[0], NULL);
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
		status = sys_file_open_n(dynamic_linker_path, dynamic_linker_path_length, &dynamic_linker_descriptor);
		if (status != ferr_ok) {
			goto out;
		}

		// read the main Mach-O header
		status = sys_file_read_retry(dynamic_linker_descriptor, 0, sizeof(dynamic_linker_header), &dynamic_linker_header, NULL);
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
		status = sys_file_read_retry(file_to_load, file_offset, sizeof(load_command), &load_command, NULL);
		if (status != ferr_ok) {
			goto out;
		}

		if (load_command.type == macho_load_command_type_segment_64) {
			++loadable_segment_count;
		} else if (load_command.type == macho_load_command_type_unix_thread) {
			// dynamically linked executables are supposed to use the "main" load command rather than "unix thread".
			// besides, how did we even get here? dynamic executables are supposed to load their dynamic linker instead.
			if (file_to_load == file && (header_to_load->flags & macho_header_flag_dynamically_linked) != 0) {
				status = ferr_invalid_argument;
				goto out;
			}

#if FERRO_ARCH == FERRO_ARCH_x86_64
			// 4 * sizeof(uint32_t) for the command type, command size, flavor, and count fields
			// 16 * sizeof(uint64_t) because `rip` is the 16th entry in the array
			status = sys_file_read_retry(file_to_load, file_offset + (4 * sizeof(uint32_t)) + (16 * sizeof(uint64_t)), sizeof(entry_address), &entry_address, NULL);
#elif FERRO_ARCH == FERRO_ARCH_aarch64
			status = sys_file_read_retry(file_to_load, file_offset + (4 * sizeof(uint32_t)) + (32 * sizeof(uint64_t)), sizeof(entry_address), &entry_address, NULL);
#else
			#error Unimplemented on this architecture
#endif
			if (status != ferr_ok) {
				goto out;
			}
		}
	}

	// allocate an information structure
	if (sys_mempool_allocate(sizeof(sys_uloader_info_t) + (sizeof(sys_uloader_loaded_segment_info_t) * loadable_segment_count), NULL, (void*)&info) != ferr_ok) {
		status = ferr_temporary_outage;
		goto out;
	}

	if (file_to_load == file) {
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
		void* load_addr = NULL;
		void* load_start_addr = NULL;

		status = sys_file_read_retry(file_to_load, file_offset, sizeof(load_command), &load_command, NULL);
		if (status != ferr_ok) {
			goto out;
		}

		if (load_command.type != macho_load_command_type_segment_64) {
			continue;
		}

		status = sys_file_read_retry(file_to_load, file_offset, sizeof(segment_64_load_command), &segment_64_load_command, NULL);
		if (status != ferr_ok) {
			goto out;
		}

		if (segment_64_load_command.initial_memory_protection == 0 && segment_64_load_command.maximum_memory_protection == 0) {
			// this is a reserved-as-invalid segment, most likely __PAGEZERO.
			// just skip it.
			// XXX: this is wrong; we should actually reserve it in the memory manager so no memory is ever allocated in this region.
			continue;
		}

		page_start = sys_page_round_down_multiple(segment_64_load_command.memory_address);

		// allocate space for the segment
		// TODO: only mark it as executable if the segment is executable
		if (sys_page_allocate(sys_page_round_up_count((segment_64_load_command.memory_address + segment_64_load_command.memory_size) - page_start), 0, &load_addr) != ferr_ok) {
			status = ferr_temporary_outage;
			goto out;
		}

		load_start_addr = (char*)load_addr + (segment_64_load_command.memory_address - page_start);

		// mark the segment as loaded already (for the purpose of tracking which ones have been allocated, in case of failure)
		++info->loaded_segment_count;
		info->loaded_segments[info->loaded_segment_count - 1].target_address = (void*)segment_64_load_command.memory_address;
		info->loaded_segments[info->loaded_segment_count - 1].aligned_target_address = (void*)page_start;
		info->loaded_segments[info->loaded_segment_count - 1].load_address = load_addr;
		info->loaded_segments[info->loaded_segment_count - 1].flags = (segment_64_load_command.initial_memory_protection & macho_memory_protection_flag_execute) ? sys_uloader_loaded_segment_flag_executable : 0;
		info->loaded_segments[info->loaded_segment_count - 1].size = segment_64_load_command.memory_size;
		info->loaded_segments[info->loaded_segment_count - 1].aligned_size = sys_page_round_up_multiple((segment_64_load_command.memory_address + segment_64_load_command.memory_size) - page_start);

		if (file_to_load == dynamic_linker_descriptor) {
			info->loaded_segments[info->loaded_segment_count - 1].flags |= sys_uloader_loaded_segment_flag_interpreter;
		}

		// read it in from the file
		status = sys_file_read_retry(file_to_load, segment_64_load_command.file_offset, segment_64_load_command.file_size, load_start_addr, NULL);
		if (status != ferr_ok) {
			goto out;
		}

		// zero out uninitialized memory
		simple_memset((char*)load_start_addr + segment_64_load_command.file_size, 0, segment_64_load_command.memory_size - segment_64_load_command.file_size);
	}

out:
	if (status == ferr_ok) {
		*out_info = info;
	} else {
		if (info) {
			for (size_t i = 0; i < info->loaded_segment_count; ++i) {
				FERRO_WUR_IGNORE(sys_page_free((void*)sys_page_round_down_multiple((uintptr_t)info->loaded_segments[i].load_address)));
			}

			FERRO_WUR_IGNORE(sys_mempool_free(info));
		}
	}
	return status;
};

static ferr_t sys_uloader_unload_file(sys_uloader_info_t* info) {
	if (!info) {
		return ferr_invalid_argument;
	}

	for (size_t i = 0; i < info->loaded_segment_count; ++i) {
		FERRO_WUR_IGNORE(sys_page_free((void*)sys_page_round_down_multiple((uintptr_t)info->loaded_segments[i].load_address)));
	}

	FERRO_WUR_IGNORE(sys_mempool_free(info));

	return ferr_ok;
};

ferr_t sys_proc_create(sys_file_t* file, void* context_block, size_t context_block_size, sys_proc_flags_t flags, sys_proc_t** out_proc) {
	ferr_t status = ferr_ok;
	sys_proc_object_t* proc = NULL;
	bool release_file_on_exit = false;
	sys_proc_id_t proc_id = SYS_PROC_ID_INVALID;
	sys_proc_handle_t proc_handle = UINT64_MAX;
	libsyscall_process_create_info_t info;
	ferro_thread_context_t context;
	libsyscall_process_memory_region_t* regions = NULL;
	size_t region_count = 0;
	sys_uloader_info_t* loader_info = NULL;
	uint64_t descriptors[1] = {UINT64_MAX};
	sys_channel_t* binary_desc = NULL;

	if (
		(!out_proc && ((flags & sys_proc_flag_resume) == 0 || (flags & sys_proc_flag_detach) == 0))
	) {
		status = ferr_invalid_argument;
		goto out;
	}

	// retain the file so it's not closed while we're using its descriptor
	status = sys_retain(file);
	if (status != ferr_ok) {
		goto out;
	}
	release_file_on_exit = true;

	if (out_proc) {
		status = sys_object_new(&proc_class, sizeof(sys_proc_object_t) - sizeof(sys_object_t), (void*)&proc);
		if (status != ferr_ok) {
			goto out;
		}

		proc->id = SYS_PROC_ID_INVALID;
		proc->detached = (flags & sys_proc_flag_detach) != 0;
	}

	// load the process

	status = sys_uloader_load_file(file, &loader_info);
	if (status != ferr_ok) {
		goto out;
	}

	status = sys_mempool_allocate(sizeof(*regions) * loader_info->loaded_segment_count, NULL, (void*)&regions);
	if (status != ferr_ok) {
		goto out;
	}

	for (size_t i = 0; i < loader_info->loaded_segment_count; ++i) {
		sys_uloader_loaded_segment_info_t* seg_info = &loader_info->loaded_segments[i];
		libsyscall_process_memory_region_t* region = &regions[i];

		region->source.start = seg_info->load_address;
		region->source.length = seg_info->aligned_size;
		region->destination = seg_info->aligned_target_address;
	}

	simple_memset(&info, 0, sizeof(info));
	simple_memset(&context, 0, sizeof(context));

	void* entry_addr = loader_info->interpreter_entry_address ? loader_info->interpreter_entry_address : loader_info->entry_address;

#if FERRO_ARCH == FERRO_ARCH_x86_64
	context.rsp = (uintptr_t)entry_addr;
#elif FERRO_ARCH == FERRO_ARCH_aarch64
	context.pc = (uintptr_t)entry_addr;
#else
	#error Unknown architecture
#endif

	// create the process binary descriptor

	status = vfs_file_duplicate_raw(((sys_file_object_t*)file)->file, &binary_desc);
	if (status != ferr_ok) {
		goto out;
	}

	descriptors[0] = ((sys_channel_object_t*)binary_desc)->channel_did;

	// create the process

	info.flags = libsyscall_process_create_flag_use_default_stack;
	info.thread_context = &context;
	info.regions = regions;
	info.region_count = region_count;
	info.descriptors = descriptors;
	info.descriptor_count = sizeof(descriptors) / sizeof(*descriptors);

	status = libsyscall_wrapper_process_create(&info, &proc_handle);
	if (status != ferr_ok) {
		goto out;
	}

	// assigning the descriptor to the new process consumes it
	((sys_channel_object_t*)binary_desc)->channel_did = SYS_CHANNEL_DID_INVALID;

	status = libsyscall_wrapper_process_id(proc_handle, &proc_id);
	if (status != ferr_ok) {
		goto out;
	}

	if (proc) {
		proc->handle = proc_handle;
		proc->id = proc_id;
	}

	if (flags & sys_proc_flag_resume) {
		// TODO: add a `flags` argument to the syscall to allow the thread to be started immediately in the kernel and avoid an extra syscall

		// this should never fail
		sys_abort_status(libsyscall_wrapper_process_resume(proc_handle));
	}

out:
	if (regions) {
		LIBSYS_WUR_IGNORE(sys_mempool_free(regions));
	}
	if (status == ferr_ok) {
		if (out_proc) {
			*out_proc = (void*)proc;
		}
	} else {
		if (proc) {
			sys_release((void*)proc);
		}
	}
	if (release_file_on_exit) {
		sys_release(file);
	}
	if (loader_info) {
		sys_uloader_unload_file(loader_info);
	}
	if (binary_desc) {
		sys_release(binary_desc);
	}
	return status;
};

ferr_t sys_proc_resume(sys_proc_t* object) {
	sys_proc_object_t* proc = (void*)object;
	return libsyscall_wrapper_process_resume(proc->handle);
};

ferr_t sys_proc_suspend(sys_proc_t* object) {
	sys_proc_object_t* proc = (void*)object;
	return libsyscall_wrapper_process_suspend(proc->handle);
};

sys_proc_t* sys_proc_current(void) {
	return (void*)this_process;
};

sys_proc_id_t sys_proc_id(sys_proc_t* object) {
	sys_proc_object_t* proc = (void*)object;
	return proc->id;
};

ferr_t sys_proc_detach(sys_proc_t* object) {
	sys_proc_object_t* proc = (void*)object;
	bool prev = proc->detached;
	proc->detached = true;
	return prev ? ferr_already_in_progress : ferr_ok;
};
