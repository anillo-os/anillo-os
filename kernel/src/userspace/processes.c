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

FERRO_STRUCT(fproc_descriptor_entry) {
	void* descriptor;
	const fproc_descriptor_class_t* descriptor_class;
};

const fproc_descriptor_class_t fproc_descriptor_class_vfs = {
	.retain = (void*)fvfs_retain,
	.release = (void*)fvfs_release,
};

static void fproc_destroy(fproc_t* process) {
	fwaitq_wake_many(&process->destroy_wait, SIZE_MAX);

	if (fmempool_free(process) != ferr_ok) {
		fpanic("Failed to free thread information structure");
	}
};

ferr_t fproc_retain(fproc_t* process) {
	return frefcount_increment(&process->reference_count);
};

void fproc_release(fproc_t* process) {
	if (frefcount_decrement(&process->reference_count) != ferr_permanent_outage) {
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

static bool fproc_clear_did_iterator(void* context, simple_ghmap_t* hashmap, simple_ghmap_hash_t hash, const void* key, size_t key_size, void* entry, size_t entry_size) {
	fproc_descriptor_entry_t* did_desc = entry;

	did_desc->descriptor_class->release(did_desc->descriptor);

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

	// clear all private futexes
	futex_table_destroy(&proc->futex_table);

	if (proc->binary_info) {
		fpanic_status(fuloader_unload_file(proc->binary_info));
	}

	proc->binary_info = NULL;

	if (proc->binary_descriptor) {
		fvfs_release(proc->binary_descriptor);
	}

	// clear all open descriptors
	// (thereby releasing all underlying descriptors)
	flock_mutex_lock(&proc->descriptor_table_mutex);
	simple_ghmap_for_each(&proc->descriptor_table, fproc_clear_did_iterator, NULL);
	simple_ghmap_destroy(&proc->descriptor_table);
	flock_mutex_unlock(&proc->descriptor_table_mutex);

	fpage_space_destroy(&proc->space);

	// get rid of our parent process waiter
	flock_mutex_lock(&proc->parent_process_mutex);
	if (proc->parent_process) {
		// NOTE: race condition here where waiter might've already been awoken but hasn't released parent yet.
		//       it's not a big deal, though; waiters are reset to unattached states upon wake-up,
		//       so un-waiting it here would have no effect.
		fwaitq_unwait(&proc->parent_process->death_wait, &proc->parent_process_death_waiter);
		fproc_release(proc->parent_process);
		proc->parent_process = NULL;
	}
	flock_mutex_unlock(&proc->parent_process_mutex);

	// clean up the mappings linked list
	// (the memory pointed to by the mappings is automatically cleaned up by fpage_space_destroy())
	flock_mutex_lock(&proc->mappings_mutex);
	fproc_mapping_t* next = NULL;
	for (fproc_mapping_t* mapping = proc->mappings; mapping != NULL; mapping = next) {
		next = mapping->next;
		FERRO_WUR_IGNORE(fmempool_free(mapping));
	}
	proc->mappings = NULL;
	flock_mutex_unlock(&proc->mappings_mutex);

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

FERRO_NO_RETURN
extern void farch_uthread_syscall_exit_preserve_all(const fthread_saved_context_t* context);

static void fproc_thread_init(void* context) {
	fproc_t* proc = context;

	if (proc->binary_descriptor) {
		void* address = proc->binary_info->interpreter_entry_address ? proc->binary_info->interpreter_entry_address : proc->binary_info->entry_address;
		futhread_jump_user_self(address);
	} else {
		// jump into the frame that's already been set up
		fint_disable();
		fpanic_status(fpage_space_swap(FARCH_PER_CPU(current_uthread_data)->user_space));
		farch_uthread_syscall_exit_preserve_all(FARCH_PER_CPU(current_uthread_data)->saved_syscall_context);
	}
};

static void fproc_parent_process_died(void* context) {
	fproc_t* proc = context;

	// keep ourselves alive until we're done, otherwise we might die while waiting for a lock
	if (fproc_retain(proc) != ferr_ok) {
		return;
	}

	flock_mutex_lock(&proc->parent_process_mutex);

	if (proc->parent_process) {
		// TODO: re-parent this process (e.g. onto the root process)
		fproc_release(proc->parent_process);
		proc->parent_process = NULL;
	}

	flock_mutex_unlock(&proc->parent_process_mutex);

	fproc_release(proc);
};

ferr_t fproc_new(fvfs_descriptor_t* file_descriptor, fproc_t* parent_process, fproc_t** out_proc) {
	fproc_t* proc = NULL;
	ferr_t status = ferr_ok;
	bool destroy_space_on_fail = false;
	futhread_data_private_t* private_data = NULL;
	bool destroy_descriptor_table_on_fail = false;
	bool destroy_per_proc_on_fail = false;
	bool destroy_futex_table_on_fail = false;
	fthread_t* first_thread = NULL;
	bool release_parent_on_fail = false;

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
	frefcount_init(&proc->reference_count);
	FERRO_WUR_IGNORE(fproc_retain(proc));

	// initialize the address space
	if (fpage_space_init(&proc->space) != ferr_ok) {
		status = ferr_temporary_outage;
		goto out;
	}

	if (file_descriptor) {
		// load the binary into the address space
		status = fuloader_load_file(file_descriptor, &proc->space, &proc->binary_info);
		if (status != ferr_ok) {
			goto out;
		}
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

	if (file_descriptor) {
		if (fvfs_retain(file_descriptor) != ferr_ok) {
			status = ferr_invalid_argument;
			goto out;
		}
		proc->binary_descriptor = file_descriptor;
	}

	if (simple_ghmap_init(&proc->descriptor_table, 16, sizeof(fproc_descriptor_entry_t), simple_ghmap_allocate_mempool, simple_ghmap_free_mempool, NULL, NULL, NULL, NULL, NULL, NULL) != ferr_ok) {
		status = ferr_temporary_outage;
		goto out;
	}
	destroy_descriptor_table_on_fail = true;

	if (simple_ghmap_init(&proc->per_proc, 16, sizeof(fper_proc_entry_t), simple_ghmap_allocate_mempool, simple_ghmap_free_mempool, NULL, NULL, NULL, NULL, NULL, NULL) != ferr_ok) {
		status = ferr_temporary_outage;
		goto out;
	}
	destroy_per_proc_on_fail = true;

	if (futex_table_init(&proc->futex_table) != ferr_ok) {
		status = ferr_temporary_outage;
		goto out;
	}
	destroy_futex_table_on_fail = true;

	if (parent_process) {
		if (fproc_retain(parent_process) != ferr_ok) {
			status = ferr_permanent_outage;
			goto out;
		}
		release_parent_on_fail = true;
	}

	proc->parent_process = parent_process;

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

	proc->next_lowest_did = 0;
	proc->highest_did = 0;

	fwaitq_init(&proc->death_wait);
	fwaitq_init(&proc->destroy_wait);

	flock_mutex_init(&proc->per_proc_mutex);

	proc->id = FPROC_ID_INVALID;

	flock_mutex_init(&proc->parent_process_mutex);

	if (parent_process) {
		// register ourselves to be notified when our parent process dies (so we can release our reference on it)
		fwaitq_waiter_init(&proc->parent_process_death_waiter, fproc_parent_process_died, proc);
		fwaitq_wait(&parent_process->death_wait, &proc->parent_process_death_waiter);
	}

out:
	if (status == ferr_ok) {
		*out_proc = proc;
	} else {
		if (release_parent_on_fail) {
			fproc_release(parent_process);
		}

		if (destroy_futex_table_on_fail) {
			futex_table_destroy(&proc->futex_table);
		}

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
static void update_next_available_did(fproc_t* process) {
	for (fproc_did_t did = process->next_lowest_did + 1; did < FPROC_DID_MAX; ++did) {
		if (simple_ghmap_lookup_h(&process->descriptor_table, did, false, SIZE_MAX, NULL, NULL, NULL) != ferr_no_such_resource) {
			continue;
		}

		process->next_lowest_did = did;
		return;
	}

	process->next_lowest_did = FPROC_DID_MAX;
};

ferr_t fproc_install_descriptor(fproc_t* process, void* descriptor, const fproc_descriptor_class_t* descriptor_class, fproc_did_t* out_did) {
	ferr_t status = ferr_ok;
	bool release_descriptor_on_fail = false;
	fproc_did_t did = FPROC_DID_MAX;
	fproc_descriptor_entry_t* did_desc = NULL;
	bool created = false;

	if (!process || !out_did) {
		status = ferr_invalid_argument;
		goto out_unlocked;
	}

	flock_mutex_lock(&process->descriptor_table_mutex);

	if (process->next_lowest_did == FPROC_DID_MAX) {
		status = ferr_temporary_outage;
		goto out;
	}

	did = process->next_lowest_did;

	if (descriptor_class->retain(descriptor) != ferr_ok) {
		status = ferr_invalid_argument;
		goto out;
	}
	release_descriptor_on_fail = true;

	if (simple_ghmap_lookup_h(&process->descriptor_table, did, true, SIZE_MAX, &created, (void*)&did_desc, NULL) != ferr_ok) {
		status = ferr_temporary_outage;
		goto out;
	}

	// shouldn't happen, but just in case
	if (!created) {
		status = ferr_temporary_outage;
		goto out;
	}

	// at this point, we can no longer fail; this DID is definitely good to go

	did_desc->descriptor = descriptor;
	did_desc->descriptor_class = descriptor_class;

	update_next_available_did(process);

	if (did > process->highest_did) {
		process->highest_did = did;
	}

	*out_did = did;

out:
	flock_mutex_unlock(&process->descriptor_table_mutex);
out_unlocked:
	if (status != ferr_ok) {
		if (release_descriptor_on_fail) {
			descriptor_class->release(descriptor);
		}
	}
	return status;
};

ferr_t fproc_uninstall_descriptor(fproc_t* process, fproc_did_t did) {
	ferr_t status = ferr_ok;
	fproc_descriptor_entry_t* did_desc = NULL;

	if (!process) {
		status = ferr_invalid_argument;
		goto out_unlocked;
	}

	flock_mutex_lock(&process->descriptor_table_mutex);

	if (simple_ghmap_lookup_h(&process->descriptor_table, did, false, SIZE_MAX, NULL, (void*)&did_desc, NULL) != ferr_ok) {
		status = ferr_no_such_resource;
		goto out;
	}

	did_desc->descriptor_class->release(did_desc->descriptor);

	// panic if this fails because we just checked above that it *does* exist
	fpanic_status(simple_ghmap_clear_h(&process->descriptor_table, did));

	if (did < process->next_lowest_did) {
		process->next_lowest_did = did;
	}

out:
	flock_mutex_unlock(&process->descriptor_table_mutex);
out_unlocked:
	return status;
};

ferr_t fproc_lookup_descriptor(fproc_t* process, fproc_did_t did, bool retain, void** out_descriptor, const fproc_descriptor_class_t** out_descriptor_class) {
	ferr_t status = ferr_ok;
	fproc_descriptor_entry_t* did_desc = NULL;

	if (!process || (retain && !out_descriptor)) {
		status = ferr_invalid_argument;
		goto out_unlocked;
	}

	flock_mutex_lock(&process->descriptor_table_mutex);

	if (simple_ghmap_lookup_h(&process->descriptor_table, did, false, SIZE_MAX, NULL, (void*)&did_desc, NULL) != ferr_ok) {
		status = ferr_no_such_resource;
		goto out;
	}

	if (retain) {
		if (did_desc->descriptor_class->retain(did_desc->descriptor) != ferr_ok) {
			// this should actually be impossible
			// it would mean that someone over-released the descriptor

			// clean up the table entry since this descriptor is garbage now
			fpanic_status(simple_ghmap_clear_h(&process->descriptor_table, did));

			if (did < process->next_lowest_did) {
				process->next_lowest_did = did;
			}

			status = ferr_no_such_resource;
			goto out;
		}
	}

	if (out_descriptor) {
		*out_descriptor = did_desc->descriptor;
	}
	if (out_descriptor_class) {
		*out_descriptor_class = did_desc->descriptor_class;
	}

out:
	flock_mutex_unlock(&process->descriptor_table_mutex);
out_unlocked:
	return status;
};

ferr_t fproc_register_mapping(fproc_t* process, void* address, size_t page_count, fproc_mapping_flags_t flags, fpage_mapping_t* backing_mapping) {
	ferr_t status = ferr_ok;
	fproc_mapping_t** prev = &process->mappings;
	fproc_mapping_t* new_mapping = NULL;

	if (!process) {
		status = ferr_invalid_argument;
		goto out_unlocked;
	}

	if (backing_mapping && fpage_mapping_retain(backing_mapping) != ferr_ok) {
		backing_mapping = NULL;
		status = ferr_permanent_outage;
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

	if (fmempool_allocate(sizeof(fproc_mapping_t), NULL, (void*)&new_mapping) != ferr_ok) {
		status = ferr_temporary_outage;
		goto out;
	}

	new_mapping->next = NULL;
	new_mapping->prev = prev;
	new_mapping->page_count = page_count;
	new_mapping->virtual_start = address;
	new_mapping->flags = flags;
	new_mapping->backing_mapping = backing_mapping;

	*prev = new_mapping;

out:
	flock_mutex_unlock(&process->mappings_mutex);
out_unlocked:
	if (status != ferr_ok) {
		if (new_mapping) {
			fpanic_status(fmempool_free(new_mapping));
		}
		if (backing_mapping) {
			fpage_mapping_release(backing_mapping);
		}
	}
	return status;
};

// the process' mappings mutex MUST be held here
static fproc_mapping_t* find_mapping(fproc_t* process, void* address) {
	fproc_mapping_t* mapping = process->mappings;

	while (mapping) {
		if (mapping->virtual_start <= address && mapping->virtual_start + (mapping->page_count * FPAGE_PAGE_SIZE) > address) {
			break;
		}

		mapping = mapping->next;
	}

	return mapping;
};

ferr_t fproc_unregister_mapping(fproc_t* process, void* address, size_t* out_page_count, fproc_mapping_flags_t* out_flags, fpage_mapping_t** out_mapping) {
	ferr_t status = ferr_ok;
	fproc_mapping_t* mapping = NULL;

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

	if (out_flags) {
		*out_flags = mapping->flags;
	}

	if (out_mapping) {
		*out_mapping = mapping->backing_mapping;
	} else if (mapping->backing_mapping) {
		fpage_mapping_release(mapping->backing_mapping);
	}

	fpanic_status(fmempool_free(mapping));

out:
	flock_mutex_unlock(&process->mappings_mutex);
out_unlocked:
	return status;
};

ferr_t fproc_lookup_mapping(fproc_t* process, void* address, size_t* out_page_count, fproc_mapping_flags_t* out_flags, fpage_mapping_t** out_mapping) {
	ferr_t status = ferr_ok;
	fproc_mapping_t* mapping = NULL;

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

	if (out_flags) {
		*out_flags = mapping->flags;
	}

	if (out_mapping) {
		// this cannot fail
		if (mapping->backing_mapping) {
			fpanic_status(fpage_mapping_retain(mapping->backing_mapping));
		}
		*out_mapping = mapping->backing_mapping;
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

	ferr_t tmp = fthread_suspend(thread, false);
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

static bool kill_each_thread(void* context, fproc_t* process, fthread_t* thread) {
	ferr_t* status_ptr = context;

	if (thread == fthread_current()) {
		// kill the current thread later
		return true;
	}

	ferr_t tmp = fthread_kill(thread);
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

ferr_t fproc_kill(fproc_t* process) {
	ferr_t status = ferr_ok;
	fproc_for_each_thread(process, kill_each_thread, &status);
	if (process == fproc_current()) {
		fthread_kill_self();
	}
	return status;
};

ferr_t fproc_attach_thread(fproc_t* proc, fthread_t* uthread) {
	futhread_data_private_t* private_data = NULL;
	ferr_t status = ferr_ok;

	// the thread gets a reference on the process...
	if (fproc_retain(proc) != ferr_ok) {
		proc = NULL;
		uthread = NULL;
		status = ferr_permanent_outage;
		goto out;
	}

	// ...and the process gets a reference on the thread
	if (fthread_retain(uthread) != ferr_ok) {
		uthread = NULL;
		status = ferr_permanent_outage;
		goto out;
	}

	// set ourselves as the process for the uthread
	private_data = (void*)futhread_data_for_thread(uthread);
	private_data->process = proc;

	// add the uthread to the process uthread list
	flock_mutex_lock(&proc->uthread_list_mutex);
	private_data->prev = &proc->uthread_list;
	private_data->next = proc->uthread_list;
	if (private_data->next) {
		private_data->next->prev = &private_data->next;
	}
	proc->uthread_list = private_data;
	flock_mutex_unlock(&proc->uthread_list_mutex);

	// register ourselves to be notified when the uthread dies (so we can release our resources)
	fwaitq_waiter_init(&private_data->uthread_death_waiter, fproc_uthread_died, private_data);
	fwaitq_waiter_init(&private_data->uthread_destroy_waiter, fproc_uthread_destroyed, proc);
	fwaitq_wait(&private_data->public.death_wait, &private_data->uthread_death_waiter);
	fwaitq_wait(&private_data->public.destroy_wait, &private_data->uthread_destroy_waiter);

	// null out the pointers so we don't release the references we acquired
	proc = NULL;
	uthread = NULL;

out:
	if (proc) {
		fproc_release(proc);
	}
	if (uthread) {
		fthread_release(uthread);
	}
	return status;
};

fproc_t* fproc_get_parent_process(fproc_t* process) {
	fproc_t* parent = NULL;

	flock_mutex_lock(&process->parent_process_mutex);

	if (process->parent_process) {
		if (fproc_retain(process->parent_process) != ferr_ok) {
			goto out;
		}

		parent = process->parent_process;
	}

out:
	flock_mutex_unlock(&process->parent_process_mutex);
	return parent;
};
