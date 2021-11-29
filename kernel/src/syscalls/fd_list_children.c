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

#include <gen/ferro/userspace/syscall-handlers.h>
#include <ferro/userspace/processes.h>
#include <ferro/core/vfs.h>
#include <ferro/core/ghmap.h>
#include <ferro/core/panic.h>

FERRO_STRUCT(per_proc_context_list_children) {
	flock_mutex_t lock;
	uint64_t next;
	simple_ghmap_t context_table;
};

FERRO_STRUCT(list_context) {
	fvfs_descriptor_t* descriptor;
	size_t current_child;
	size_t current_child_name_offset;
	size_t count;
	fvfs_list_children_context_t context;
	fvfs_path_t listed_children[16];
};

static fper_proc_key_t context_key;

void fsyscall_init_fd_list_children(void) {
	fpanic_status(fper_proc_register(&context_key));
};

static ferr_t per_proc_context_list_children_init(per_proc_context_list_children_t* per_process_context) {
	flock_mutex_init(&per_process_context->lock);
	per_process_context->next = 0;
	return simple_ghmap_init(&per_process_context->context_table, 16, sizeof(list_context_t), simple_ghmap_allocate_mempool, simple_ghmap_free_mempool, NULL, NULL, NULL, NULL, NULL, NULL);
};

static void per_proc_context_list_children_destroy(void* context, void* entry, size_t entry_size) {
	per_proc_context_list_children_t* per_process_context = entry;

	simple_ghmap_destroy(&per_process_context->context_table);
};

ferr_t fsyscall_handler_fd_list_children_init(uint64_t fd, uint64_t* out_context) {
	fvfs_descriptor_t* descriptor = NULL;
	ferr_t status = ferr_ok;
	bool created = false;
	per_proc_context_list_children_t* per_process_context = NULL;
	uint64_t context = 0;
	list_context_t* list_context = NULL;

	// TODO: more robust userspace address checks (e.g. check for validity and writability)
	if (!out_context) {
		status = ferr_invalid_argument;
	}

	if (fproc_lookup_descriptor(fproc_current(), fd, true, &descriptor) != ferr_ok) {
		status = ferr_invalid_argument;
		goto out_unlocked;
	}

	if (fper_proc_lookup(fproc_current(), context_key, true, sizeof(per_proc_context_list_children_t), per_proc_context_list_children_destroy, NULL, &created, (void*)&per_process_context, NULL) != ferr_ok) {
		status = ferr_temporary_outage;
		goto out_unlocked;
	}

	if (created) {
		// FIXME: there's a race condition between the context being created and it being initialized
		if (per_proc_context_list_children_init(per_process_context) != ferr_ok) {
			FERRO_WUR_IGNORE(fper_proc_clear(fproc_current(), context_key, true));
			status = ferr_temporary_outage;
			goto out_unlocked;
		}
	}

	flock_mutex_lock(&per_process_context->lock);

	context = per_process_context->next++;

	if (simple_ghmap_lookup_h(&per_process_context->context_table, context, true, SIZE_MAX, &created, (void*)&list_context, NULL) != ferr_ok) {
		status = ferr_temporary_outage;
		goto out;
	}

	if (!created) {
		// this would be *super* weird
		status = ferr_temporary_outage;
		goto out;
	}

	status = fvfs_list_children_init(descriptor, &list_context->listed_children[0], sizeof(list_context->listed_children) / sizeof(*list_context->listed_children), true, &list_context->count, &list_context->context);
	if (status != ferr_ok) {
		FERRO_WUR_IGNORE(simple_ghmap_clear_h(&per_process_context->context_table, context));
		goto out;
	}

	// transfer ownership of the retained reference to the descriptor into the list context
	// (and nullify the local descriptor variable so the reference won't be released on return)
	list_context->descriptor = descriptor;
	descriptor = NULL;

	*(uint64_t*)out_context = context;

out:
	flock_mutex_unlock(&per_process_context->lock);
out_unlocked:
	if (descriptor != NULL) {
		fvfs_release(descriptor);
	}
	return status;
};

ferr_t fsyscall_handler_fd_list_children_finish(uint64_t context) {
	ferr_t status = ferr_ok;
	per_proc_context_list_children_t* per_process_context = NULL;
	list_context_t* list_context = NULL;

	if (fper_proc_lookup(fproc_current(), context_key, false, 0, NULL, NULL, NULL, (void*)&per_process_context, NULL) != ferr_ok) {
		status = ferr_no_such_resource;
		goto out_unlocked;
	}

	flock_mutex_lock(&per_process_context->lock);

	if (simple_ghmap_lookup_h(&per_process_context->context_table, context, false, SIZE_MAX, NULL, (void*)&list_context, NULL) != ferr_ok) {
		status = ferr_no_such_resource;
		goto out;
	}

	FERRO_WUR_IGNORE(fvfs_list_children_finish(list_context->descriptor, &list_context->listed_children[0], list_context->count, &list_context->context));

	fvfs_release(list_context->descriptor);

	FERRO_WUR_IGNORE(simple_ghmap_clear_h(&per_process_context->context_table, context));

out:
	flock_mutex_unlock(&per_process_context->lock);
out_unlocked:
	return status;
};

ferr_t fsyscall_handler_fd_list_children(uint64_t context, uint64_t buffer_size, void* xout_buffer, uint64_t* out_read_count) {
	ferr_t status = ferr_ok;
	per_proc_context_list_children_t* per_process_context = NULL;
	list_context_t* list_context = NULL;
	char* out_buffer = xout_buffer;
	size_t buffer_index = 0;

	// TODO: more robust address checks
	if (buffer_size == 0 || !xout_buffer) {
		status = ferr_invalid_argument;
		goto out_unlocked;
	}

	if (fper_proc_lookup(fproc_current(), context_key, false, 0, NULL, NULL, NULL, (void*)&per_process_context, NULL) != ferr_ok) {
		status = ferr_no_such_resource;
		goto out_unlocked;
	}

	flock_mutex_lock(&per_process_context->lock);

	if (simple_ghmap_lookup_h(&per_process_context->context_table, context, false, SIZE_MAX, NULL, (void*)&list_context, NULL) != ferr_ok) {
		status = ferr_no_such_resource;
		goto out;
	}

	// this loop is guaranteed to run at least once because we require @p buffer_size to be greater than 0
	for (; buffer_index < buffer_size; ++buffer_index) {
		if (list_context->current_child >= list_context->count) {
			// if we've reached the end of the currently listed children, try to get more
			status = fvfs_list_children(list_context->descriptor, &list_context->listed_children[0], sizeof(list_context->listed_children) / sizeof(*list_context->listed_children), true, &list_context->count, &list_context->context);
			if (status != ferr_ok) {
				// if we read at least one character into the buffer, consider the call a success.
				// we can try to get more entries next time userspace decides to call us again.
				// if we still fail then, that's when we'll report the failure.
				if (buffer_index > 0) {
					status = ferr_ok;
				}
				goto out;
			}
			list_context->current_child = 0;
			list_context->current_child_name_offset = 0;
		}

		fvfs_path_t* current_child = &list_context->listed_children[list_context->current_child];

		if (list_context->current_child_name_offset >= current_child->length) {
			// if we've reached the end of this child's name, write a null terminator and advance to the next child
			out_buffer[buffer_index] = '\0';
			list_context->current_child_name_offset = 0;
			++list_context->current_child;
			continue;
		}

		// otherwise, write the current character from this child's name and continue to the next character
		out_buffer[buffer_index] = current_child->path[list_context->current_child_name_offset++];
	}

out:
	flock_mutex_unlock(&per_process_context->lock);
out_unlocked:
	return status;
};
