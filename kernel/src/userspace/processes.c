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

#include <ferro/userspace/processes.h>
#include <ferro/core/mempool.h>
#include <ferro/core/panic.h>
#include <ferro/userspace/threads.private.h>
#include <ferro/userspace/syscalls.h>
#include <ferro/userspace/loader.h>
#include <ferro/core/scheduler.h>

static void fproc_destroy(fproc_t* process) {
	if (fmempool_free(process) != ferr_ok) {
		fpanic("Failed to free thread information structure");
	}
};

ferr_t fproc_retain(fproc_t* process) {
	uint64_t old_value = __atomic_load_n(&process->reference_count, __ATOMIC_RELAXED);

	do {
		if (old_value == 0) {
			return ferr_permanent_outage;
		}
	} while (!__atomic_compare_exchange_n(&process->reference_count, &old_value, old_value + 1, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED));

	return ferr_ok;
};

void fproc_release(fproc_t* process) {
	uint64_t old_value = __atomic_load_n(&process->reference_count, __ATOMIC_RELAXED);

	do {
		if (old_value == 0) {
			return;
		}
	} while (!__atomic_compare_exchange_n(&process->reference_count, &old_value, old_value - 1, false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED));

	if (old_value != 1) {
		return;
	}

	fproc_destroy(process);
};

fproc_t* fproc_current(void) {
	futhread_data_private_t* private_data = (void*)futhread_data_for_thread(futhread_current());

	if (!private_data) {
		return NULL;
	}

	return private_data->process;
};

static bool fproc_clear_fd_iterator(void* context, simple_ghmap_t* hashmap, simple_ghmap_hash_t hash, void* entry) {
	fvfs_descriptor_t** desc_ptr = entry;

	fvfs_release(*desc_ptr);

	return true;
};

static void fproc_all_uthreads_died(fproc_t* proc) {
	fpanic_status(fuloader_unload_file(proc->binary_info));

	proc->binary_info = NULL;

	fpage_space_destroy(&proc->space);

	fvfs_release(proc->binary_descriptor);

	// clear all open descriptors
	// (thereby releasing all underlying VFS descriptors)
	flock_mutex_lock(&proc->descriptor_table_mutex);
	simple_ghmap_for_each(&proc->descriptor_table, fproc_clear_fd_iterator, NULL);
	simple_ghmap_destroy(&proc->descriptor_table);
	flock_mutex_unlock(&proc->descriptor_table_mutex);

	// alright, now that it's been cleaned up, the process can be released
	fproc_release(proc);
};

static void fproc_uthread_died(void* context) {
	fproc_t* proc = context;

	// TODO: once we support multiple threads in a process, we'll need to check if this is the last thread that's dying

	// retain the process so that it lives long enough to be cleaned up
	fpanic_status(fproc_retain(proc));

	// never do this before retaining the process because it may lead to a full release of the process
	fthread_release(proc->thread);

	fproc_all_uthreads_died(proc);
};

static void fproc_uthread_destroyed(void* context) {
	fproc_t* proc = context;

	// now that the uthread has been destroyed and there's no chance of anyone using the reference it has on us, release the reference
	fproc_release(proc);
};

static void fproc_thread_init(void* context) {
	fproc_t* proc = context;
	void* address = proc->binary_info->interpreter_entry_address ? proc->binary_info->interpreter_entry_address : proc->binary_info->entry_address;

	futhread_jump_user_self(address);
};

ferr_t fproc_new(fvfs_descriptor_t* file_descriptor, fproc_t** out_proc) {
	fproc_t* proc = NULL;
	ferr_t status = ferr_ok;
	bool destroy_space_on_fail = false;
	futhread_data_private_t* private_data = NULL;
	bool destroy_descriptor_table_on_fail = false;

	if (!out_proc) {
		status = ferr_invalid_argument;
		goto out;
	}

	// allocate the information structure
	if (fmempool_allocate(sizeof(fproc_t), NULL, (void*)&proc) != ferr_ok) {
		status = ferr_temporary_outage;
		goto out;
	}

	proc->thread = NULL;
	proc->binary_info = NULL;
	proc->binary_descriptor = NULL;

	// the user initially has one reference and so does the uthread
	// the uthread's reference lasts until it is destroyed
	proc->reference_count = 2;

	// initialize the address space
	if (fpage_space_init(&proc->space) != ferr_ok) {
		status = ferr_temporary_outage;
		goto out;
	}

	// load the binary into the address space
	status = fuloader_load_file(file_descriptor, &proc->space, &proc->binary_info);
	if (status != ferr_ok) {
		goto out;
	}

	// create the first thread
	if (fthread_new(fproc_thread_init, proc, NULL, FPAGE_LARGE_PAGE_SIZE, 0, &proc->thread) != ferr_ok) {
		status = ferr_temporary_outage;
		goto out;
	}

	if (fsched_manage(proc->thread) != ferr_ok) {
		status = ferr_temporary_outage;
		goto out;
	}

	// register a userspace context onto the new thread
	if (futhread_register(proc->thread, FPAGE_LARGE_PAGE_SIZE, &proc->space, 0, fsyscall_table_handler, (void*)&fsyscall_table_standard) != ferr_ok) {
		status = ferr_temporary_outage;
		goto out;
	}

	if (fvfs_retain(file_descriptor) != ferr_ok) {
		status = ferr_invalid_argument;
		goto out;
	}
	proc->binary_descriptor = file_descriptor;

	if (simple_ghmap_init(&proc->descriptor_table, 16, sizeof(fvfs_descriptor_t*), simple_ghmap_allocate_mempool, simple_ghmap_free_mempool, NULL, NULL) != ferr_ok) {
		status = ferr_temporary_outage;
		goto out;
	}
	destroy_descriptor_table_on_fail = true;

	// if we got here, this process is definitely okay.
	// just a few more non-erroring-throwing tasks to do and then we're done.

	// set ourselves as the process for the uthread
	private_data = (void*)futhread_data_for_thread(proc->thread);
	private_data->process = proc;

	// register ourselves to be notified when the uthread dies (so we can release our resources)
	fwaitq_waiter_init(&proc->uthread_death_waiter, fproc_uthread_died, proc);
	fwaitq_waiter_init(&proc->uthread_destroy_waiter, fproc_uthread_destroyed, proc);
	fwaitq_wait(&private_data->public.death_wait, &proc->uthread_death_waiter);
	fwaitq_wait(&private_data->public.destroy_wait, &proc->uthread_destroy_waiter);

	proc->mappings = NULL;

	flock_mutex_init(&proc->mappings_mutex);
	flock_mutex_init(&proc->descriptor_table_mutex);

	proc->next_lowest_fd = 0;
	proc->highest_fd = 0;

out:
	if (status == ferr_ok) {
		*out_proc = proc;
	} else {
		if (destroy_descriptor_table_on_fail) {
			simple_ghmap_destroy(&proc->descriptor_table);
		}

		if (proc->binary_descriptor) {
			fvfs_release(proc->binary_descriptor);
		}

		if (proc->thread) {
			fthread_release(proc->thread);
		}

		if (proc->binary_info) {
			fpanic_status(fuloader_unload_file(proc->binary_info));
		}

		if (destroy_space_on_fail) {
			fpage_space_destroy(&proc->space);
		}

		fpanic_status(fmempool_free(proc));
	}
	return status;
};

// the process's descriptor table mutex MUST be held
static void update_next_available_fd(fproc_t* process) {
	for (fproc_fd_t fd = process->next_lowest_fd + 1; fd < FPROC_FD_MAX; ++fd) {
		if (simple_ghmap_lookup_h(&process->descriptor_table, fd, false, NULL, NULL) != ferr_no_such_resource) {
			continue;
		}

		process->next_lowest_fd = fd;
		return;
	}

	process->next_lowest_fd = FPROC_FD_MAX;
};

ferr_t fproc_install_descriptor(fproc_t* process, fvfs_descriptor_t* descriptor, fproc_fd_t* out_fd) {
	ferr_t status = ferr_ok;
	bool release_descriptor_on_fail = false;
	fproc_fd_t fd = FPROC_FD_MAX;
	fvfs_descriptor_t** fd_desc = NULL;
	bool created = false;

	if (!process || !out_fd) {
		status = ferr_invalid_argument;
		goto out_unlocked;
	}

	flock_mutex_lock(&process->descriptor_table_mutex);

	if (process->next_lowest_fd == FPROC_FD_MAX) {
		status = ferr_temporary_outage;
		goto out;
	}

	fd = process->next_lowest_fd;

	if (fvfs_retain(descriptor) != ferr_ok) {
		status = ferr_invalid_argument;
		goto out;
	}
	release_descriptor_on_fail = true;

	if (simple_ghmap_lookup_h(&process->descriptor_table, fd, true, &created, (void*)&fd_desc) != ferr_ok) {
		status = ferr_temporary_outage;
		goto out;
	}

	// shouldn't happen, but just in case
	if (!created) {
		status = ferr_temporary_outage;
		goto out;
	}

	// at this point, we can no longer fail; this FD is definitely good to go

	*fd_desc = descriptor;

	update_next_available_fd(process);

	if (fd > process->highest_fd) {
		process->highest_fd = fd;
	}

	*out_fd = fd;

out:
	flock_mutex_unlock(&process->descriptor_table_mutex);
out_unlocked:
	if (release_descriptor_on_fail) {
		fvfs_release(descriptor);
	}
	return status;
};

ferr_t fproc_uninstall_descriptor(fproc_t* process, fproc_fd_t fd) {
	ferr_t status = ferr_ok;
	fvfs_descriptor_t** fd_desc = NULL;

	if (!process) {
		status = ferr_invalid_argument;
		goto out_unlocked;
	}

	flock_mutex_lock(&process->descriptor_table_mutex);

	if (simple_ghmap_lookup_h(&process->descriptor_table, fd, false, NULL, (void*)&fd_desc) != ferr_ok) {
		status = ferr_no_such_resource;
		goto out;
	}

	fvfs_release(*fd_desc);

	// panic if this fails because we just checked above that it *does* exist
	fpanic_status(simple_ghmap_clear_h(&process->descriptor_table, fd));

	if (fd < process->next_lowest_fd) {
		process->next_lowest_fd = fd;
	}

out:
	flock_mutex_unlock(&process->descriptor_table_mutex);
out_unlocked:
	return status;
};

ferr_t fproc_lookup_descriptor(fproc_t* process, fproc_fd_t fd, bool retain, fvfs_descriptor_t** out_descriptor) {
	ferr_t status = ferr_ok;
	fvfs_descriptor_t** fd_desc = NULL;

	if (!process || (retain && !out_descriptor)) {
		status = ferr_invalid_argument;
		goto out_unlocked;
	}

	flock_mutex_lock(&process->descriptor_table_mutex);

	if (simple_ghmap_lookup_h(&process->descriptor_table, fd, false, NULL, (void*)&fd_desc) != ferr_ok) {
		status = ferr_no_such_resource;
		goto out;
	}

	if (retain) {
		if (fvfs_retain(*fd_desc) != ferr_ok) {
			// this should actually be impossible
			// it would mean that someone over-released the VFS descriptor

			// clean up the table entry since this descriptor is garbage now
			fpanic_status(simple_ghmap_clear_h(&process->descriptor_table, fd));

			if (fd < process->next_lowest_fd) {
				process->next_lowest_fd = fd;
			}

			status = ferr_no_such_resource;
			goto out;
		}
	}

	if (out_descriptor) {
		*out_descriptor = *fd_desc;
	}

out:
	flock_mutex_unlock(&process->descriptor_table_mutex);
out_unlocked:
	return status;
};

ferr_t fproc_register_mapping(fproc_t* process, void* address, size_t page_count) {
	ferr_t status = ferr_ok;
	fpage_mapping_t** prev = &process->mappings;
	fpage_mapping_t* new_mapping = NULL;

	if (!process) {
		status = ferr_invalid_argument;
		goto out_unlocked;
	}

	flock_mutex_lock(&process->mappings_mutex);

	while (*prev != NULL) {
		if ((*prev)->virtual_start <= address && (uintptr_t)(*prev)->virtual_start + ((*prev)->page_count * FPAGE_PAGE_SIZE) > (uintptr_t)address) {
			status = ferr_already_in_progress;
			goto out;
		}

		prev = &(*prev)->next;
	}

	if (fmempool_allocate(sizeof(fpage_mapping_t), NULL, (void*)&new_mapping) != ferr_ok) {
		status = ferr_temporary_outage;
		goto out;
	}

	new_mapping->next = NULL;
	new_mapping->prev = prev;
	new_mapping->page_count = page_count;
	new_mapping->virtual_start = address;

	*prev = new_mapping;

out:
	flock_mutex_unlock(&process->mappings_mutex);
out_unlocked:
	if (status != ferr_ok) {
		if (new_mapping) {
			fpanic_status(fmempool_free(new_mapping));
		}
	}
	return status;
};

// the process' mappings mutex MUST be held here
static fpage_mapping_t* find_mapping(fproc_t* process, void* address) {
	fpage_mapping_t* mapping = process->mappings;

	while (mapping) {
		if (mapping->virtual_start == address) {
			break;
		}

		mapping = mapping->next;
	}

	return mapping;
};

ferr_t fproc_unregister_mapping(fproc_t* process, void* address, size_t* out_page_count) {
	ferr_t status = ferr_ok;
	fpage_mapping_t* mapping = NULL;

	if (!process) {
		status = ferr_invalid_argument;
		goto out_unlocked;
	}

	flock_mutex_lock(&process->mappings_mutex);

	mapping = find_mapping(process, address);

	if (!mapping) {
		status = ferr_no_such_resource;
		goto out;
	}

	*mapping->prev = mapping->next;
	if (mapping->next) {
		mapping->next->prev = mapping->prev;
	}

	if (out_page_count) {
		*out_page_count = mapping->page_count;
	}

	fpanic_status(fmempool_free(mapping));

out:
	flock_mutex_unlock(&process->mappings_mutex);
out_unlocked:
	return status;
};

ferr_t fproc_lookup_mapping(fproc_t* process, void* address, size_t* out_page_count) {
	ferr_t status = ferr_ok;
	fpage_mapping_t* mapping = NULL;

	if (!process) {
		status = ferr_invalid_argument;
		goto out_unlocked;
	}

	flock_mutex_lock(&process->mappings_mutex);

	mapping = find_mapping(process, address);

	if (!mapping) {
		status = ferr_no_such_resource;
		goto out;
	}

	if (out_page_count) {
		*out_page_count = mapping->page_count;
	}

out:
	flock_mutex_unlock(&process->mappings_mutex);
out_unlocked:
	return status;
};
