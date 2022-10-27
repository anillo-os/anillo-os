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
		sys_abort_status(libsyscall_wrapper_process_kill(proc->id));
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

	status = libsyscall_wrapper_process_id(&this_process->id);
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

ferr_t sys_proc_create(sys_file_t* file, void* context_block, size_t context_block_size, sys_proc_flags_t flags, sys_proc_t** out_proc) {
	ferr_t status = ferr_ok;
	sys_proc_object_t* proc = NULL;
	sys_fd_t fd = SYS_FD_INVALID;
	bool release_file_on_exit = false;
	sys_proc_id_t proc_id = SYS_PROC_ID_INVALID;

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

	// now get the underlying descriptor
	status = sys_file_fd(file, &fd);
	if (status != ferr_ok) {
		goto out;
	}

	if (out_proc) {
		status = sys_object_new(&proc_class, sizeof(sys_proc_object_t) - sizeof(sys_object_t), (void*)&proc);
		if (status != ferr_ok) {
			goto out;
		}

		proc->id = SYS_PROC_ID_INVALID;
		proc->detached = (flags & sys_proc_flag_detach) != 0;
	}

	status = libsyscall_wrapper_process_create(fd, context_block, context_block_size, &proc_id);
	if (status != ferr_ok) {
		goto out;
	}

	if (proc) {
		proc->id = proc_id;
	}

	if (flags & sys_proc_flag_resume) {
		// TODO: add a `flags` argument to the syscall to allow the thread to be started immediately in the kernel and avoid an extra syscall

		// this should never fail
		sys_abort_status(libsyscall_wrapper_process_resume(proc_id));
	}

out:
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
	return status;
};

ferr_t sys_proc_resume(sys_proc_t* object) {
	sys_proc_object_t* proc = (void*)object;
	return libsyscall_wrapper_process_resume(proc->id);
};

ferr_t sys_proc_suspend(sys_proc_t* object) {
	sys_proc_object_t* proc = (void*)object;
	return libsyscall_wrapper_process_suspend(proc->id);
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
