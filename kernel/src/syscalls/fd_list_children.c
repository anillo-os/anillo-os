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
#include <ferro/core/mempool.h>
#include <libsimple/libsimple.h>

FERRO_STRUCT(list_context) {
	fvfs_descriptor_t* descriptor;
	size_t current_child;
	size_t current_child_name_offset;
	size_t count;
	fvfs_list_children_context_t context;
	fvfs_path_t listed_children[16];
	frefcount_t refcount;
};

static ferr_t list_context_retain(void* ctx) {
	list_context_t* context = ctx;
	return frefcount_increment(&context->refcount);
};

static void list_context_release(void* ctx) {
	list_context_t* context = ctx;

	if (frefcount_decrement(&context->refcount) == ferr_permanent_outage) {
		if (context->descriptor) {
			fvfs_release(context->descriptor);
		}

		FERRO_WUR_IGNORE(fmempool_free(context));
	}
};

static const fproc_descriptor_class_t list_context_class = {
	.retain = list_context_retain,
	.release = list_context_release,
};

void fsyscall_init_fd_list_children(void) {

};

ferr_t fsyscall_handler_fd_list_children_init(uint64_t fd, uint64_t* out_context) {
	fvfs_descriptor_t* descriptor = NULL;
	ferr_t status = ferr_ok;
	fproc_did_t did = 0;
	list_context_t* list_context = NULL;
	const fproc_descriptor_class_t* desc_class = NULL;
	bool uninstall_descriptor_on_fail = false;
	bool finish_context_on_fail = false;

	// TODO: more robust userspace address checks (e.g. check for validity and writability)
	if (!out_context) {
		status = ferr_invalid_argument;
	}

	if (fproc_lookup_descriptor(fproc_current(), fd, true, (void*)&descriptor, &desc_class) != ferr_ok) {
		status = ferr_invalid_argument;
		goto out;
	}

	if (desc_class != &fproc_descriptor_class_vfs) {
		status = ferr_invalid_argument;
		goto out;
	}

	status = fmempool_allocate(sizeof(*list_context), NULL, (void*)&list_context);
	if (status != ferr_ok) {
		goto out;
	}

	simple_memset(list_context, 0, sizeof(*list_context));

	frefcount_init(&list_context->refcount);

	status = fvfs_list_children_init(descriptor, &list_context->listed_children[0], sizeof(list_context->listed_children) / sizeof(*list_context->listed_children), true, &list_context->count, &list_context->context);
	if (status != ferr_ok) {
		goto out;
	}

	finish_context_on_fail = true;

	// transfer ownership of the retained reference to the descriptor into the list context
	// (and nullify the local descriptor variable so the reference won't be released on return)
	list_context->descriptor = descriptor;
	descriptor = NULL;

	status = fproc_install_descriptor(fproc_current(), list_context, &list_context_class, &did);
	if (status != ferr_ok) {
		goto out;
	}

	uninstall_descriptor_on_fail = true;

	*(uint64_t*)out_context = did;

out:
	if (descriptor != NULL) {
		fvfs_release(descriptor);
	}
	if (status != ferr_ok) {
		if (finish_context_on_fail) {
			FERRO_WUR_IGNORE(fvfs_list_children_finish(list_context->descriptor, &list_context->listed_children[0], list_context->count, &list_context->context));
		}
		if (uninstall_descriptor_on_fail) {
			FERRO_WUR_IGNORE(fproc_uninstall_descriptor(fproc_current(), did));
		}
	}
	if (list_context) {
		list_context_release(list_context);
	}
	return status;
};

ferr_t fsyscall_handler_fd_list_children_finish(uint64_t context) {
	ferr_t status = ferr_ok;
	list_context_t* list_context = NULL;
	const fproc_descriptor_class_t* desc_class = NULL;

	status = fproc_lookup_descriptor(fproc_current(), context, true, (void*)&list_context, &desc_class);
	if (status != ferr_ok) {
		status = ferr_no_such_resource;
		goto out;
	}

	if (desc_class != &list_context_class) {
		status = ferr_invalid_argument;
		goto out;
	}

	FERRO_WUR_IGNORE(fvfs_list_children_finish(list_context->descriptor, &list_context->listed_children[0], list_context->count, &list_context->context));

	status = fproc_uninstall_descriptor(fproc_current(), context);
	if (status != ferr_ok) {
		goto out;
	}

out:
	if (list_context) {
		desc_class->release(list_context);
	}
	return status;
};

ferr_t fsyscall_handler_fd_list_children(uint64_t context, uint64_t buffer_size, void* xout_buffer, uint64_t* out_read_count) {
	ferr_t status = ferr_ok;
	list_context_t* list_context = NULL;
	char* out_buffer = xout_buffer;
	size_t buffer_index = 0;
	const fproc_descriptor_class_t* desc_class = NULL;

	// TODO: more robust address checks
	if (buffer_size == 0 || !xout_buffer) {
		status = ferr_invalid_argument;
		goto out;
	}

	if (fproc_lookup_descriptor(fproc_current(), context, true, (void*)&list_context, &desc_class) != ferr_ok) {
		status = ferr_no_such_resource;
		goto out;
	}

	if (desc_class != &list_context_class) {
		status = ferr_invalid_argument;
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
	if (list_context) {
		desc_class->release(list_context);
	}
	return status;
};
