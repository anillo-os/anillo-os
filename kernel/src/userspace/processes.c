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

#include <ferro/userspace/processes.private.h>
#include <ferro/core/mempool.h>
#include <ferro/core/panic.h>
#include <ferro/userspace/threads.private.h>
#include <ferro/userspace/syscalls.h>
#include <ferro/userspace/loader.h>
#include <ferro/core/scheduler.h>
#include <stdatomic.h>

FERRO_STRUCT(fper_proc_entry) {
	fper_proc_data_destructor_f destructor;
	void* destructor_context;
	char data[];
};

static void fproc_destroy(fproc_t* process) {
	fwaitq_wake_many(&process->destroy_wait, SIZE_MAX);

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

static bool fproc_clear_fd_iterator(void* context, simple_ghmap_t* hashmap, simple_ghmap_hash_t hash, const void* key, size_t key_size, void* entry, size_t entry_size) {
	fvfs_descriptor_t** desc_ptr = entry;

	fvfs_release(*desc_ptr);

	return true;
};

static bool per_proc_clear_iterator(void* context, simple_ghmap_t* hashmap, simple_ghmap_hash_t hash, const void* key, size_t key_size, void* entry, size_t entry_size) {
	fper_proc_entry_t* per_proc_entry = entry;

	if (per_proc_entry->destructor) {
		per_proc_entry->destructor(per_proc_entry->destructor_context, per_proc_entry->data, entry_size - sizeof(fper_proc_entry_t));
	}

	return true;
};

static void fproc_all_uthreads_died(fproc_t* proc) {
	fwaitq_wake_many(&proc->death_wait, SIZE_MAX);

	// clear all per-process data
	flock_mutex_lock(&proc->per_proc_mutex);
	simple_ghmap_for_each(&proc->per_proc, per_proc_clear_iterator, NULL);
	simple_ghmap_destroy(&proc->per_proc);
	flock_mutex_unlock(&proc->per_proc_mutex);

	fpanic_status(fuloader_unload_file(proc->binary_info));

	proc->binary_info = NULL;

	fvfs_release(proc->binary_descriptor);

	// clear all open descriptors
	// (thereby releasing all underlying VFS descriptors)
	flock_mutex_lock(&proc->descriptor_table_mutex);
	simple_ghmap_for_each(&proc->descriptor_table, fproc_clear_fd_iterator, NULL);
	simple_ghmap_destroy(&proc->descriptor_table);
	flock_mutex_unlock(&proc->descriptor_table_mutex);

	fpage_space_destroy(&proc->space);

	// alright, now that it's been cleaned up, the process can be released
	fproc_release(proc);
};

void fproc_uthread_died(void* context) {
	futhread_data_private_t* uthread_private = context;
	fproc_t* proc = uthread_private->process;
	bool is_last = false;

	// retain the process so that it lives long enough to be cleaned up
	fpanic_status(fproc_retain(proc));

	// remove the dead uthread from the uthread list
	flock_mutex_lock(&proc->uthread_list_mutex);
	*uthread_private->prev = uthread_private->next;
	if (uthread_private->next) {
		uthread_private->next->prev = uthread_private->prev;
	}
	is_last = (proc->uthread_list == NULL) ? true : false;
	flock_mutex_unlock(&proc->uthread_list_mutex);

	// never do this before retaining the process because it may lead to a full release of the process
	fthread_release(uthread_private->thread);

	if (is_last) {
		// this was the last thread; let's clean up the process
		fproc_all_uthreads_died(proc);
	} else {
		// there are still more threads left; release our extra retain from earlier
		fproc_release(proc);
	}
};

void fproc_uthread_destroyed(void* context) {
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
	bool destroy_per_proc_on_fail = false;
	fthread_t* first_thread = NULL;

	if (!out_proc) {
		status = ferr_invalid_argument;
		goto out;
	}

	// allocate the information structure
	if (fmempool_allocate(sizeof(fproc_t), NULL, (void*)&proc) != ferr_ok) {
		status = ferr_temporary_outage;
		goto out;
	}

	proc->uthread_list = NULL;
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
	if (fthread_new(fproc_thread_init, proc, NULL, FPAGE_LARGE_PAGE_SIZE, 0, &first_thread) != ferr_ok) {
		status = ferr_temporary_outage;
		goto out;
	}

	if (fsched_manage(first_thread) != ferr_ok) {
		status = ferr_temporary_outage;
		goto out;
	}

	// register a userspace context onto the new thread
	if (futhread_register(first_thread, NULL, FPAGE_LARGE_PAGE_SIZE, &proc->space, 0, fsyscall_table_handler, (void*)&fsyscall_table_standard) != ferr_ok) {
		status = ferr_temporary_outage;
		goto out;
	}

	if (fvfs_retain(file_descriptor) != ferr_ok) {
		status = ferr_invalid_argument;
		goto out;
	}
	proc->binary_descriptor = file_descriptor;

	if (simple_ghmap_init(&proc->descriptor_table, 16, sizeof(fvfs_descriptor_t*), simple_ghmap_allocate_mempool, simple_ghmap_free_mempool, NULL, NULL, NULL, NULL, NULL, NULL) != ferr_ok) {
		status = ferr_temporary_outage;
		goto out;
	}
	destroy_descriptor_table_on_fail = true;

	if (simple_ghmap_init(&proc->per_proc, 16, sizeof(fper_proc_entry_t), simple_ghmap_allocate_mempool, simple_ghmap_free_mempool, NULL, NULL, NULL, NULL, NULL, NULL) != ferr_ok) {
		status = ferr_temporary_outage;
		goto out;
	}
	destroy_per_proc_on_fail = true;

	// if we got here, this process is definitely okay.
	// just a few more non-erroring-throwing tasks to do and then we're done.

	// set ourselves as the process for the uthread
	private_data = (void*)futhread_data_for_thread(first_thread);
	private_data->process = proc;

	flock_mutex_init(&proc->uthread_list_mutex);
	private_data->prev = &proc->uthread_list;
	private_data->next = NULL;
	proc->uthread_list = private_data;

	// register ourselves to be notified when the uthread dies (so we can release our resources)
	fwaitq_waiter_init(&private_data->uthread_death_waiter, fproc_uthread_died, private_data);
	fwaitq_waiter_init(&private_data->uthread_destroy_waiter, fproc_uthread_destroyed, proc);
	fwaitq_wait(&private_data->public.death_wait, &private_data->uthread_death_waiter);
	fwaitq_wait(&private_data->public.destroy_wait, &private_data->uthread_destroy_waiter);

	proc->mappings = NULL;

	flock_mutex_init(&proc->mappings_mutex);
	flock_mutex_init(&proc->descriptor_table_mutex);

	proc->next_lowest_fd = 0;
	proc->highest_fd = 0;

	fwaitq_init(&proc->death_wait);
	fwaitq_init(&proc->destroy_wait);

	flock_mutex_init(&proc->per_proc_mutex);

out:
	if (status == ferr_ok) {
		*out_proc = proc;
	} else {
		if (destroy_per_proc_on_fail) {
			simple_ghmap_destroy(&proc->per_proc);
		}

		if (destroy_descriptor_table_on_fail) {
			simple_ghmap_destroy(&proc->descriptor_table);
		}

		if (proc->binary_descriptor) {
			fvfs_release(proc->binary_descriptor);
		}

		if (first_thread) {
			fthread_release(first_thread);
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
		if (simple_ghmap_lookup_h(&process->descriptor_table, fd, false, SIZE_MAX, NULL, NULL, NULL) != ferr_no_such_resource) {
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

	if (simple_ghmap_lookup_h(&process->descriptor_table, fd, true, SIZE_MAX, &created, (void*)&fd_desc, NULL) != ferr_ok) {
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

	if (simple_ghmap_lookup_h(&process->descriptor_table, fd, false, SIZE_MAX, NULL, (void*)&fd_desc, NULL) != ferr_ok) {
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

	if (simple_ghmap_lookup_h(&process->descriptor_table, fd, false, SIZE_MAX, NULL, (void*)&fd_desc, NULL) != ferr_ok) {
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

ferr_t fper_proc_register(fper_proc_key_t* out_key) {
	static _Atomic fper_proc_key_t key_counter = 0;
	if (!out_key) {
		return ferr_invalid_argument;
	}
	*out_key = key_counter++;
	return ferr_ok;
};

ferr_t fper_proc_lookup(fproc_t* process, fper_proc_key_t key, bool create_if_absent, size_t size_if_absent, fper_proc_data_destructor_f destructor_if_absent, void* destructor_context, bool* out_created, void** out_pointer, size_t* out_size) {
	ferr_t status = ferr_ok;
	flock_mutex_lock(&process->per_proc_mutex);

	fper_proc_entry_t* entry = NULL;
	bool created = false;
	size_t entry_size = 0;

	status = simple_ghmap_lookup_h(&process->per_proc, key, create_if_absent, sizeof(fper_proc_entry_t) + size_if_absent, &created, (void*)&entry, &entry_size);
	if (status != ferr_ok) {
		goto out;
	}

	if (created) {
		entry->destructor = destructor_if_absent;
		entry->destructor_context = destructor_context;
	}

	if (out_size) {
		*out_size = entry_size - sizeof(fper_proc_entry_t);
	}

	if (out_pointer) {
		*out_pointer = &entry->data[0];
	}

	if (out_created) {
		*out_created = created;
	}

out:
	flock_mutex_unlock(&process->per_proc_mutex);
	return status;
};

ferr_t fper_proc_clear(fproc_t* process, fper_proc_key_t key, bool skip_previous_destructor) {
	ferr_t status = ferr_ok;
	flock_mutex_lock(&process->per_proc_mutex);

	fper_proc_entry_t* entry = NULL;
	size_t entry_size = 0;

	if (simple_ghmap_lookup_h(&process->per_proc, key, false, SIZE_MAX, NULL, (void*)&entry, &entry_size) != ferr_ok) {
		status = ferr_no_such_resource;
		goto out;
	}

	if (entry->destructor) {
		entry->destructor(entry->destructor_context, entry->data, entry_size - sizeof(fper_proc_entry_t));
	}

	status = simple_ghmap_clear_h(&process->per_proc, key);

out:
	flock_mutex_unlock(&process->per_proc_mutex);
	return status;
};

ferr_t fproc_for_each_thread(fproc_t* process, fproc_for_each_thread_iterator_f iterator, void* context) {
	ferr_t status = ferr_ok;
	flock_mutex_lock(&process->uthread_list_mutex);

	for (futhread_data_private_t* private_data = process->uthread_list; private_data != NULL; private_data = private_data->next) {
		if (!iterator(context, process, private_data->thread)) {
			status = ferr_cancelled;
			break;
		}
	}

	flock_mutex_unlock(&process->uthread_list_mutex);
	return status;
};

static bool suspend_each_thread(void* context, fproc_t* process, fthread_t* thread) {
	ferr_t* status_ptr = context;

	if (thread == fthread_current()) {
		// suspend the current thread later
		return true;
	}

	ferr_t tmp = fthread_suspend(thread);
	switch (tmp) {
		case ferr_ok:
		case ferr_already_in_progress:
		case ferr_permanent_outage:
			break;
		default:
			fpanic_status(tmp);
	}

	return true;
};

static bool resume_each_thread(void* context, fproc_t* process, fthread_t* thread) {
	ferr_t* status_ptr = context;

	// there's no way that we can be resuming the current thread
	//if (thread == fthread_current()) {
	//	return true;
	//}

	ferr_t tmp = fthread_resume(thread);
	switch (tmp) {
		case ferr_ok:
		case ferr_already_in_progress:
		case ferr_permanent_outage:
			break;
		default:
			fpanic_status(tmp);
	}

	return true;
};

ferr_t fproc_suspend(fproc_t* process) {
	ferr_t status = ferr_ok;
	fproc_for_each_thread(process, suspend_each_thread, &status);
	if (process == fproc_current()) {
		fthread_suspend_self();
	}
	return status;
};

ferr_t fproc_resume(fproc_t* process) {
	ferr_t status = ferr_ok;
	fproc_for_each_thread(process, resume_each_thread, &status);
	// we can't be resuming the current process
	//if (process == fproc_current()) {
	//	fthread_suspend_self();
	//}
	return status;
};
