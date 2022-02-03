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

#include <libsys/threads.private.h>
#include <libsys/locks.h>
#include <gen/libsyscall/syscall-wrappers.h>
#include <libsys/objects.private.h>
#include <libsys/abort.h>
#include <libsys/config.h>
#include <libsimple/libsimple.h>

static void sys_thread_destroy(sys_thread_t* object);
extern void __sys_thread_setup(sys_thread_object_t* thread);
void __sys_thread_exit_self(void);
extern void __sys_thread_entry(void);

static const sys_object_class_t thread_class = {
	.destroy = sys_thread_destroy,
};

ferr_t sys_thread_init(void) {
	ferr_t status = ferr_ok;
	sys_thread_object_t* thread = NULL;

	status = sys_object_new(&thread_class, sizeof(sys_thread_object_t) - sizeof(sys_object_t), (void*)&thread);
	if (status != ferr_ok) {
		goto out;
	}

	thread->id = SYS_THREAD_ID_INVALID;

	simple_memset(thread->tls, 0, sizeof(thread->tls));

	thread->tls[sys_thread_tls_key_tls] = &thread->tls[sys_thread_tls_key_tls];
	thread->tls[sys_thread_tls_key_self] = thread;

	status = libsyscall_wrapper_thread_id(&thread->id);
	if (status != ferr_ok) {
		goto out;
	}

	__sys_thread_setup(thread);

out:
	if (status == ferr_ok) {

	} else {
		if (thread) {
			sys_release((void*)thread);
		}
	}
	return status;
};

static void sys_thread_destroy(sys_thread_t* object) {
	sys_thread_object_t* thread = (void*)object;

	if (thread->id != SYS_THREAD_ID_INVALID) {
		sys_abort_status(libsyscall_wrapper_thread_kill(thread->id));
	}
};

ferr_t sys_thread_create(void* stack, size_t stack_size, sys_thread_entry_f entry, void* context, sys_thread_flags_t flags, sys_thread_t** out_thread) {
	ferr_t status = ferr_ok;
	sys_thread_object_t* thread = NULL;
	uintptr_t stack_top = (uintptr_t)stack + stack_size;

	if (
		(stack_size < sys_config_read_minimum_stack_size())    ||
		(!out_thread && (flags & sys_thread_flag_resume) == 0)
	) {
		status = ferr_invalid_argument;
		goto out;
	}

	status = sys_object_new(&thread_class, sizeof(sys_thread_object_t) - sizeof(sys_object_t), (void*)&thread);
	if (status != ferr_ok) {
		goto out;
	}

	thread->id = SYS_THREAD_ID_INVALID;

	simple_memset(thread->tls, 0, sizeof(thread->tls));

	thread->tls[sys_thread_tls_key_tls] = &thread->tls[sys_thread_tls_key_tls];
	thread->tls[sys_thread_tls_key_self] = thread;

	// set up the stack
	*(void**)(stack_top - 0x08) = entry;
	*(void**)(stack_top - 0x10) = context;
	*(void**)(stack_top - 0x18) = thread;

	status = libsyscall_wrapper_thread_create(stack, stack_size, __sys_thread_entry, &thread->id);
	if (status != ferr_ok) {
		goto out;
	}

	if (flags & sys_thread_flag_resume) {
		// TODO: add a `flags` argument to the syscall to allow the thread to be started immediately in the kernel and avoid an extra syscall

		// this should never fail
		sys_abort_status(libsyscall_wrapper_thread_resume(thread->id));
	}

out:
	if (status == ferr_ok) {
		if (out_thread) {
			LIBSYS_WUR_IGNORE(sys_retain((void*)thread));
			*out_thread = (void*)thread;
		}
	} else {
		if (thread) {
			sys_release((void*)thread);
		}
	}
	return status;
};

ferr_t sys_thread_resume(sys_thread_t* object) {
	sys_thread_object_t* thread = (void*)object;
	return libsyscall_wrapper_thread_resume(thread->id);
};

ferr_t sys_thread_suspend(sys_thread_t* object) {
	sys_thread_object_t* thread = (void*)object;
	return libsyscall_wrapper_thread_suspend(thread->id, 0, 0);
};

ferr_t sys_thread_suspend_timeout(sys_thread_t* object, uint64_t timeout, sys_thread_timeout_type_t timeout_type) {
	sys_thread_object_t* thread = (void*)object;
	return libsyscall_wrapper_thread_suspend(thread->id, timeout, timeout_type + 1);
};

sys_thread_t* sys_thread_current(void) {
	return *(void* LIBSYS_FS_RELATIVE*)(sys_thread_tls_key_self * sizeof(void*));
};

sys_thread_id_t sys_thread_id(sys_thread_t* object) {
	sys_thread_object_t* thread = (void*)object;
	return thread->id;
};

void __sys_thread_exit_self(void) {
	sys_thread_object_t* thread = (void*)sys_thread_current();
	sys_abort_status(libsyscall_wrapper_thread_kill(thread->id));
	__builtin_unreachable();
};
