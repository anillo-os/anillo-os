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
#include <libsys/pages.private.h>

static sys_proc_object_t* this_process = NULL;

static void sys_proc_destroy(sys_proc_t* object);

static const sys_object_class_t proc_class = {
	LIBSYS_OBJECT_CLASS_INTERFACE(NULL),
	.destroy = sys_proc_destroy,
};

static sys_channel_object_t proc_init_channel = {
	.object = {
		.object_class = &__sys_object_class_channel,
		.reference_count = 0,
		.flags = sys_object_flag_immortal,
	},

	// the process initialization channel is *always* DID 1
	// (just like the VFS binary descriptor is always DID 0)
	.channel_did = 1,
};

static sys_once_t proc_init_channel_once = SYS_ONCE_INITIALIZER;
static sys_channel_message_t* proc_init_message = NULL;
static sys_mutex_t proc_init_message_mutex = SYS_MUTEX_INIT;

static void sys_proc_init_receive_message(void* context) {
	sys_abort_status_log(sys_channel_receive((void*)&proc_init_channel, sys_channel_receive_flag_no_wait, &proc_init_message));
};

LIBSYS_OBJECT_CLASS_GETTER(proc, proc_class);

static void sys_proc_destroy(sys_proc_t* object) {
	sys_proc_object_t* proc = (void*)object;

	if (proc->id != SYS_PROC_ID_INVALID) {
		if (proc->detached) {
			sys_abort_status(libsyscall_wrapper_process_close(proc->handle));
		} else {
			sys_abort_status(libsyscall_wrapper_process_kill(proc->handle));
		}
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

#if !defined(BUILDING_DYMPLE) && !defined(BUILDING_STATIC)
	sys_once(&proc_init_channel_once, sys_proc_init_receive_message, NULL, 0);
#endif

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
	sys_data_t* cmd_data = NULL;
	char* cmd_data_ptr = NULL;

	macho_load_command_t* load_command = NULL;
	size_t file_offset = 0;
	size_t cmd_offset = 0;

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

	// read all the load commands
	status = sys_file_read_data(file, sizeof(header), header.total_command_size, &cmd_data);
	if (status != ferr_ok) {
		goto out;
	}

	if (sys_data_length(cmd_data) != header.total_command_size) {
		status = ferr_unknown;
		goto out;
	}

	cmd_data_ptr = sys_data_contents(cmd_data);

	if ((header.flags & macho_header_flag_dynamically_linked) != 0) {
		// this is a dynamically linked executable, meaning we'll need to load the dynamic linker instead
		// (and it will, in turn, load the executable)

		char* dynamic_linker_path = NULL;
		size_t dynamic_linker_path_length = SIZE_MAX;
		sys_data_t* old_cmd_data = NULL;

		// let's look for the dynamic linker command
		cmd_offset = 0;
		for (size_t i = 0; i < header.command_count; (++i), (cmd_offset += load_command->size)) {
			macho_load_command_dynamic_linker_t* dynamic_linker_load_command = NULL;
			size_t name_length = 0;

			load_command = (void*)(cmd_data_ptr + cmd_offset);

			if (load_command->type != macho_load_command_type_load_dynamic_linker) {
				continue;
			}

			dynamic_linker_load_command = (void*)load_command;

			name_length = load_command->size - dynamic_linker_load_command->name_offset;

			dynamic_linker_path = (void*)((char*)dynamic_linker_load_command + dynamic_linker_load_command->name_offset);

			// the name can include zero padding at the end, so find the real length
			dynamic_linker_path_length = simple_strnlen(dynamic_linker_path, name_length);
			break;
		}

		if (!dynamic_linker_path) {
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

		old_cmd_data = cmd_data;

		// read all the load commands
		status = sys_file_read_data(dynamic_linker_descriptor, sizeof(dynamic_linker_header), dynamic_linker_header.total_command_size, &cmd_data);
		if (status != ferr_ok) {
			goto out;
		}

		if (sys_data_length(cmd_data) != dynamic_linker_header.total_command_size) {
			status = ferr_unknown;
			sys_release(old_cmd_data);
			old_cmd_data = NULL;
			goto out;
		}

		cmd_data_ptr = sys_data_contents(cmd_data);

		header_to_load = &dynamic_linker_header;
		file_to_load = dynamic_linker_descriptor;

		sys_release(old_cmd_data);
		old_cmd_data = NULL;
	}

	// determine how many loadable segments we have and what the entry address is
	cmd_offset = 0;
	for (size_t i = 0; i < header_to_load->command_count; (++i), (cmd_offset += load_command->size)) {
		load_command = (void*)(cmd_data_ptr + cmd_offset);

		if (load_command->type == macho_load_command_type_segment_64) {
			++loadable_segment_count;
		} else if (load_command->type == macho_load_command_type_unix_thread) {
			void** entry_addr_ptr;

			// dynamically linked executables are supposed to use the "main" load command rather than "unix thread".
			// besides, how did we even get here? dynamic executables are supposed to load their dynamic linker instead.
			if (file_to_load == file && (header_to_load->flags & macho_header_flag_dynamically_linked) != 0) {
				status = ferr_invalid_argument;
				goto out;
			}

#if FERRO_ARCH == FERRO_ARCH_x86_64
			// 4 * sizeof(uint32_t) for the command type, command size, flavor, and count fields
			// 16 * sizeof(uint64_t) because `rip` is the 16th entry in the array
			entry_addr_ptr = (void*)(cmd_data_ptr + cmd_offset + (4 * sizeof(uint32_t)) + (16 * sizeof(uint64_t)));
#elif FERRO_ARCH == FERRO_ARCH_aarch64
			entry_addr_ptr = (void*)(cmd_data_ptr + cmd_offset + (4 * sizeof(uint32_t)) + (32 * sizeof(uint64_t)));
#else
			#error Unimplemented on this architecture
#endif

			entry_address = *entry_addr_ptr;
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
	cmd_offset = 0;
	for (size_t i = 0; i < header_to_load->command_count; (++i), (cmd_offset += load_command->size)) {
		macho_load_command_segment_64_t* segment_64_load_command = NULL;
		uintptr_t page_start = 0;
		void* load_addr = NULL;
		void* load_start_addr = NULL;

		load_command = (void*)(cmd_data_ptr + cmd_offset);

		if (load_command->type != macho_load_command_type_segment_64) {
			continue;
		}

		segment_64_load_command = (void*)load_command;

		if (segment_64_load_command->initial_memory_protection == 0 && segment_64_load_command->maximum_memory_protection == 0) {
			// this is a reserved-as-invalid segment, most likely __PAGEZERO.
			// just skip it.
			// XXX: this is wrong; we should actually reserve it in the memory manager so no memory is ever allocated in this region.
			continue;
		}

		page_start = sys_page_round_down_multiple(segment_64_load_command->memory_address);

		// allocate space for the segment
		// TODO: only mark it as executable if the segment is executable
		if (sys_page_allocate(sys_page_round_up_count((segment_64_load_command->memory_address + segment_64_load_command->memory_size) - page_start), 0, &load_addr) != ferr_ok) {
			status = ferr_temporary_outage;
			goto out;
		}

		load_start_addr = (char*)load_addr + (segment_64_load_command->memory_address - page_start);

		// mark the segment as loaded already (for the purpose of tracking which ones have been allocated, in case of failure)
		++info->loaded_segment_count;
		info->loaded_segments[info->loaded_segment_count - 1].target_address = (void*)segment_64_load_command->memory_address;
		info->loaded_segments[info->loaded_segment_count - 1].aligned_target_address = (void*)page_start;
		info->loaded_segments[info->loaded_segment_count - 1].load_address = load_addr;
		info->loaded_segments[info->loaded_segment_count - 1].flags = (segment_64_load_command->initial_memory_protection & macho_memory_protection_flag_execute) ? sys_uloader_loaded_segment_flag_executable : 0;
		info->loaded_segments[info->loaded_segment_count - 1].size = segment_64_load_command->memory_size;
		info->loaded_segments[info->loaded_segment_count - 1].aligned_size = sys_page_round_up_multiple((segment_64_load_command->memory_address + segment_64_load_command->memory_size) - page_start);

		if (file_to_load == dynamic_linker_descriptor) {
			info->loaded_segments[info->loaded_segment_count - 1].flags |= sys_uloader_loaded_segment_flag_interpreter;
		}

		// read it in from the file
		status = sys_file_read_retry(file_to_load, segment_64_load_command->file_offset, segment_64_load_command->file_size, load_start_addr, NULL);
		if (status != ferr_ok) {
			goto out;
		}

		// zero out uninitialized memory
		simple_memset((char*)load_start_addr + segment_64_load_command->file_size, 0, segment_64_load_command->memory_size - segment_64_load_command->file_size);
	}

out:
	if (cmd_data) {
		sys_release(cmd_data);
	}
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

ferr_t sys_proc_create(sys_file_t* file, sys_object_t** attached_objects, size_t attached_object_count, sys_proc_flags_t flags, sys_proc_t** out_proc) {
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
	uint64_t descriptors[2] = {UINT64_MAX, UINT64_MAX};
	sys_channel_t* binary_desc = NULL;
	sys_channel_t* our_channel = NULL;
	sys_channel_t* their_channel = NULL;
	sys_channel_message_t* init_message = NULL;
	size_t successfully_attached_object_count = 0;
	bool receive_message_on_fail = false;

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

	region_count = loader_info->loaded_segment_count;

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
	context.rip = (uintptr_t)entry_addr;
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

	status = sys_channel_create_pair(&our_channel, &their_channel);
	if (status != ferr_ok) {
		goto out;
	}

	descriptors[1] = ((sys_channel_object_t*)their_channel)->channel_did;

	status = sys_channel_message_create(0, &init_message);
	if (status != ferr_ok) {
		goto out;
	}

	for (; successfully_attached_object_count < attached_object_count; ++successfully_attached_object_count) {
		sys_object_t** object_ptr = &attached_objects[successfully_attached_object_count];
		sys_object_t* object = *object_ptr;
		sys_channel_message_attachment_index_t attachment_index;
		const sys_object_class_t* obj_class = sys_object_class(object);

		if (obj_class == sys_object_class_channel()) {
			status = sys_channel_message_attach_channel(init_message, object, &attachment_index);
			if (status == ferr_ok) {
				*object_ptr = NULL;
			}
		} else if (obj_class == sys_object_class_server_channel()) {
			status = sys_channel_message_attach_server_channel(init_message, object, &attachment_index);
			if (status == ferr_ok) {
				*object_ptr = NULL;
			}
		} else if (obj_class == sys_object_class_data()) {
			status = sys_channel_message_attach_data(init_message, object, false, &attachment_index);
		} else if (obj_class == sys_object_class_shared_memory()) {
			status = sys_channel_message_attach_shared_memory(init_message, object, &attachment_index);
		} else {
			status = ferr_invalid_argument;
		}

		if (status != ferr_ok) {
			goto out;
		}

		fassert(attachment_index == successfully_attached_object_count);
	}

	status = sys_channel_send(our_channel, sys_channel_send_flag_no_wait, init_message, NULL);
	if (status != ferr_ok) {
		goto out;
	}

	// successfully sending the message consumes it; if we fail from now on, we have to receive the message from the other side and detach the items from that message instead.
	init_message = NULL;
	receive_message_on_fail = true;

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
	((sys_channel_object_t*)their_channel)->channel_did = SYS_CHANNEL_DID_INVALID;

	// this should never fail
	sys_abort_status(libsyscall_wrapper_process_id(proc_handle, &proc_id));

	if (proc) {
		proc->handle = proc_handle;
		proc->id = proc_id;
	}

	if (flags & sys_proc_flag_resume) {
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
		if (receive_message_on_fail) {
			sys_abort_status(sys_channel_receive(their_channel, sys_channel_receive_flag_no_wait, &init_message));
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
	if (init_message) {
		for (size_t i = 0; i < successfully_attached_object_count; ++i) {
			switch (sys_channel_message_attachment_type(init_message, i)) {
				case sys_channel_message_attachment_type_channel:
					sys_abort_status(sys_channel_message_detach_channel(init_message, i, &attached_objects[i]));
					break;
				case sys_channel_message_attachment_type_server_channel:
					sys_abort_status(sys_channel_message_detach_server_channel(init_message, i, &attached_objects[i]));
					break;
				default:
					break;
			}
		}

		sys_release(init_message);
	}
	if (our_channel) {
		sys_release(our_channel);
	}
	if (their_channel) {
		sys_release(their_channel);
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

uint64_t sys_proc_init_context_object_count(void) {
	return sys_channel_message_attachment_count(proc_init_message);
};

ferr_t sys_proc_init_context_object_class(uint64_t object_index, const sys_object_class_t** out_object_class) {
	ferr_t status = ferr_ok;
	sys_channel_message_attachment_type_t type = sys_channel_message_attachment_type_invalid;
	const sys_object_class_t* object_class = NULL;

	sys_mutex_lock(&proc_init_message_mutex);

	status = sys_channel_message_attachment_type(proc_init_message, object_index);
	if (status != ferr_ok) {
		goto out;
	}

	switch (type) {
		case sys_channel_message_attachment_type_channel:
			object_class = sys_object_class_channel();
			break;
		case sys_channel_message_attachment_type_shared_memory:
			object_class = sys_object_class_shared_memory();
			break;
		case sys_channel_message_attachment_type_data:
			object_class = sys_object_class_data();
			break;
		case sys_channel_message_attachment_type_server_channel:
			object_class = sys_object_class_server_channel();
			break;
		default:
			status = ferr_invalid_argument;
			goto out;
	}

	if (out_object_class) {
		*out_object_class = object_class;
	}

out:
	sys_mutex_unlock(&proc_init_message_mutex);
	return status;
};

ferr_t sys_proc_init_context_detach_object(uint64_t object_index, sys_object_t** out_object) {
	ferr_t status = ferr_ok;
	sys_channel_message_attachment_type_t type = sys_channel_message_attachment_type_invalid;

	sys_mutex_lock(&proc_init_message_mutex);

	status = sys_channel_message_attachment_type(proc_init_message, object_index);
	if (status != ferr_ok) {
		goto out;
	}

	switch (type) {
		case sys_channel_message_attachment_type_channel:
			status = sys_channel_message_detach_channel(proc_init_message, object_index, out_object);
			break;
		case sys_channel_message_attachment_type_shared_memory:
			status = sys_channel_message_detach_channel(proc_init_message, object_index, out_object);
			break;
		case sys_channel_message_attachment_type_data:
			status = sys_channel_message_detach_channel(proc_init_message, object_index, out_object);
			break;
		case sys_channel_message_attachment_type_server_channel:
			status = sys_channel_message_detach_server_channel(proc_init_message, object_index, out_object);
			break;
		default:
			status = ferr_invalid_argument;
			goto out;
	}

out:
	sys_mutex_unlock(&proc_init_message_mutex);
	return status;
};
