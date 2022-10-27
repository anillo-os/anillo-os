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
#include <libsys/locks.private.h>
#include <gen/libsyscall/syscall-wrappers.h>
#include <libsys/objects.private.h>
#include <libsys/abort.h>
#include <libsys/config.h>
#include <libsimple/libsimple.h>
#include <libsys/pages.h>
#include <libsys/tls.h>
#include <libsys/ghmap.h>

// the guaranteed minimum number of TLS keys that each thread can store.
// FIXME: we need to guarantee that, if you're able to create a TLS key, you're able to store to it in any thread.
#define MIN_GUARANTEED_TLS 128

static void sys_thread_destroy(sys_thread_t* object);
extern void __sys_thread_setup(sys_thread_object_t* thread);
void __sys_thread_exit_self(void);
extern void __sys_thread_entry(void);

__attribute__((noreturn))
extern void __sys_thread_die(sys_thread_id_t thread_id, void* free_this);

static ferr_t sys_tls_init(void);
static ferr_t sys_tls_cleanup(sys_thread_object_t* thread);

static const sys_object_class_t thread_class = {
	LIBSYS_OBJECT_CLASS_INTERFACE(NULL),
	.destroy = sys_thread_destroy,
};

LIBSYS_OBJECT_CLASS_GETTER(thread, thread_class);

void __sys_thread_setup_common(void) {
	sys_thread_object_t* thread = (void*)sys_thread_current();
	libsyscall_wrapper_futex_associate(&thread->death_event.internal, 0, 0, sys_event_state_set);
};

ferr_t sys_thread_init(void) {
	ferr_t status = ferr_ok;
	sys_thread_object_t* thread = NULL;

	status = sys_object_new(&thread_class, sizeof(sys_thread_object_t) - sizeof(sys_object_t), (void*)&thread);
	if (status != ferr_ok) {
		goto out;
	}

	thread->id = SYS_THREAD_ID_INVALID;
	thread->free_on_death = NULL;

	simple_memset(thread->tls, 0, sizeof(thread->tls));

	thread->tls[sys_thread_tls_key_tls] = &thread->tls[sys_thread_tls_key_tls];
	thread->tls[sys_thread_tls_key_self] = thread;

	status = libsyscall_wrapper_thread_id(&thread->id);
	if (status != ferr_ok) {
		goto out;
	}

	status = simple_ghmap_init(&thread->external_tls, MIN_GUARANTEED_TLS, 0, simple_ghmap_allocate_sys_mempool, simple_ghmap_free_sys_mempool, NULL, NULL, NULL, NULL, NULL, NULL);
	if (status != ferr_ok) {
		goto out;
	}

	__sys_thread_setup(thread);

	status = sys_tls_init();
	if (status != ferr_ok) {
		goto out;
	}

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
		if (((sys_thread_object_t*)sys_thread_current())->id == thread->id) {
			// this is fine;
			// the thread is releasing itself
		} else {
			// this should never actually happen;
			// the thread should be holding a reference on itself
			sys_abort();
		}
	}

	simple_ghmap_destroy(&thread->external_tls);

	sys_object_destroy(object);
};

ferr_t sys_thread_create(void* stack, size_t stack_size, sys_thread_entry_f entry, void* context, sys_thread_flags_t flags, sys_thread_t** out_thread) {
	ferr_t status = ferr_ok;
	sys_thread_object_t* thread = NULL;
	uintptr_t stack_top;
	bool free_stack_on_fail = false;

	if (!stack) {
		status = sys_page_allocate(sys_page_round_up_count(stack_size), 0, &stack);
		if (status != ferr_ok) {
			goto out;
		}
		free_stack_on_fail = true;
	}

	stack_top = (uintptr_t)stack + stack_size;

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

	status = simple_ghmap_init(&thread->external_tls, 16, 0, simple_ghmap_allocate_sys_mempool, simple_ghmap_free_sys_mempool, NULL, NULL, NULL, NULL, NULL, NULL);
	if (status != ferr_ok) {
		goto out;
	}

	thread->id = SYS_THREAD_ID_INVALID;

	simple_memset(thread->tls, 0, sizeof(thread->tls));

	thread->tls[sys_thread_tls_key_tls] = &thread->tls[sys_thread_tls_key_tls];
	thread->tls[sys_thread_tls_key_self] = thread;

	thread->free_on_death = free_stack_on_fail ? stack : NULL;

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
		if (free_stack_on_fail) {
			LIBSYS_WUR_IGNORE(sys_page_free(stack));
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

ferr_t sys_thread_suspend_timeout(sys_thread_t* object, uint64_t timeout, sys_timeout_type_t timeout_type) {
	sys_thread_object_t* thread = (void*)object;
	return libsyscall_wrapper_thread_suspend(thread->id, timeout, timeout_type);
};

sys_thread_id_t sys_thread_id(sys_thread_t* object) {
	sys_thread_object_t* thread = (void*)object;
	return thread->id;
};

void __sys_thread_exit_self(void) {
	sys_thread_object_t* thread = (void*)sys_thread_current();
	sys_thread_id_t id = thread->id;
	void* free_this = thread->free_on_death;

	sys_tls_cleanup(thread);

	thread->id = SYS_THREAD_ID_INVALID;
	sys_release((void*)thread);
	__sys_thread_die(id, free_this);
};

ferr_t sys_thread_wait(sys_thread_t* object) {
	sys_thread_object_t* thread = (void*)object;

	if (object == sys_thread_current()) {
		return ferr_invalid_argument;
	}

	sys_event_wait(&thread->death_event);

	return ferr_ok;
};

//
// external TLS
//

static sys_mutex_t tls_destructor_table_mutex = SYS_MUTEX_INIT;
static simple_ghmap_t tls_destructor_table;
static sys_tls_key_t next_key = 0;

static ferr_t sys_tls_init(void) {
	ferr_t status = ferr_ok;

	status = simple_ghmap_init(&tls_destructor_table, MIN_GUARANTEED_TLS, 0, simple_ghmap_allocate_sys_mempool, simple_ghmap_free_sys_mempool, NULL, NULL, NULL, NULL, NULL, NULL);
	if (status != ferr_ok) {
		goto out;
	}

out:
	return status;
};

static bool sys_tls_cleanup_iterator(void* context, simple_ghmap_t* hashmap, simple_ghmap_hash_t hash, const void* key, size_t key_size, void* entry, size_t entry_size) {
	sys_tls_key_t tls_key = hash;
	sys_tls_value_t* value = entry;
	sys_tls_destructor_f* destructor_ptr;
	sys_tls_destructor_f destructor = NULL;
	ferr_t status = ferr_ok;

	sys_mutex_lock(&tls_destructor_table_mutex);
	status = simple_ghmap_lookup_h(&tls_destructor_table, tls_key, false, 0, NULL, (void*)&destructor_ptr, NULL);
	if (status == ferr_ok) {
		destructor = *destructor_ptr;
	}
	sys_mutex_unlock(&tls_destructor_table_mutex);

	if (destructor) {
		destructor(*value);
	}

	return true;
};

static ferr_t sys_tls_cleanup(sys_thread_object_t* thread) {
	simple_ghmap_for_each(&thread->external_tls, sys_tls_cleanup_iterator, NULL);
	return ferr_ok;
};

ferr_t sys_tls_key_create(sys_tls_destructor_f destructor, sys_tls_key_t* out_key) {
	ferr_t status = ferr_ok;
	sys_tls_key_t key;
	bool created = false;
	sys_tls_destructor_f* stored_destructor;

	sys_mutex_lock(&tls_destructor_table_mutex);

	key = next_key;
	++next_key;

	if (destructor) {
		status = simple_ghmap_lookup_h(&tls_destructor_table, key, true, sizeof(destructor), &created, (void*)&stored_destructor, NULL);
		if (status == ferr_ok) {
			if (created) {
				*stored_destructor = destructor;
			} else {
				sys_abort();
			}
		}
	}

	sys_mutex_unlock(&tls_destructor_table_mutex);

out:
	if (status == ferr_ok) {
		*out_key = key;
	}
	return status;
};

ferr_t sys_tls_get(sys_tls_key_t key, sys_tls_value_t* out_value) {
	ferr_t status = ferr_ok;
	sys_thread_object_t* thread = (void*)sys_thread_current();
	sys_tls_value_t* val_ptr;

	status = simple_ghmap_lookup_h(&thread->external_tls, key, false, 0, NULL, (void*)&val_ptr, NULL);
	if (status != ferr_ok) {
		goto out;
	}

	if (out_value) {
		*out_value = *val_ptr;
	}

out:
	return status;
};

ferr_t sys_tls_set(sys_tls_key_t key, sys_tls_value_t value) {
	ferr_t status = ferr_ok;
	sys_thread_object_t* thread = (void*)sys_thread_current();
	sys_tls_value_t* val_ptr;
	bool created = false;

	status = simple_ghmap_lookup_h(&thread->external_tls, key, true, sizeof(*val_ptr), &created, (void*)&val_ptr, NULL);
	if (status != ferr_ok) {
		goto out;
	}

	*val_ptr = value;

out:
	return status;
};

ferr_t sys_tls_unset(sys_tls_key_t key) {
	sys_thread_object_t* thread = (void*)sys_thread_current();
	return simple_ghmap_clear_h(&thread->external_tls, key);
};
