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
#include <ferro/platform.h>

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

bool __sys_thread_init_complete = false;

static simple_ghmap_t global_thread_map;
static sys_mutex_t global_thread_map_mutex = SYS_MUTEX_INIT;

LIBSYS_OBJECT_CLASS_GETTER(thread, thread_class);

void __sys_thread_setup_common(void) {
	sys_thread_object_t* thread = (void*)sys_thread_current();
	libsyscall_wrapper_futex_associate(&thread->death_event.internal, 0, 0, sys_event_state_set);
};

ferr_t sys_thread_init(void) {
	ferr_t status = ferr_ok;
	sys_thread_object_t* thread = NULL;
	libsyscall_signal_mapping_t signal_mapping;
	sys_thread_id_t thread_id = SYS_THREAD_ID_INVALID;
	bool created = false;

	status = simple_ghmap_init(&global_thread_map, 16, 0, simple_ghmap_allocate_sys_mempool, simple_ghmap_free_sys_mempool, NULL, NULL, NULL, NULL, NULL, NULL);
	if (status != ferr_ok) {
		goto out;
	}

	status = libsyscall_wrapper_thread_id(&thread_id);
	if (status != ferr_ok) {
		goto out;
	}

	status = simple_ghmap_lookup_h(&global_thread_map, thread_id, true, sizeof(*thread), &created, (void*)&thread, NULL);
	if (status != ferr_ok) {
		goto out;
	}

	if (!created) {
		// this is impossible
		sys_abort();
	}

	status = sys_object_init(&thread->object, &thread_class);
	if (status != ferr_ok) {
		goto out;
	}

	thread->id = thread_id;
	thread->free_on_death = NULL;
	thread->block_signals = false;
	thread->signal_block_count = 0;

	simple_memset(thread->tls, 0, sizeof(thread->tls));

	thread->tls[sys_thread_tls_key_tls] = &thread->tls[sys_thread_tls_key_tls];
	thread->tls[sys_thread_tls_key_self] = thread;

	status = simple_ghmap_init(&thread->external_tls, MIN_GUARANTEED_TLS, 0, simple_ghmap_allocate_sys_mempool, simple_ghmap_free_sys_mempool, NULL, NULL, NULL, NULL, NULL, NULL);
	if (status != ferr_ok) {
		goto out;
	}

	status = simple_ghmap_init(&thread->signal_handlers, 32, 0, simple_ghmap_allocate_sys_mempool, simple_ghmap_free_sys_mempool, NULL, NULL, NULL, NULL, NULL, NULL);
	if (status != ferr_ok) {
		goto out;
	}

	signal_mapping.block_all_flag = &thread->block_signals;

	// these are mapped to 0, meaning their default actions (which is killing us) should be taken instead
	signal_mapping.bus_error_signal = 0;
	signal_mapping.page_fault_signal = 0;
	signal_mapping.floating_point_exception_signal = 0;
	signal_mapping.illegal_instruction_signal = 0;
	signal_mapping.debug_signal = 0;

	status = libsyscall_wrapper_thread_signal_update_mapping(thread->id, &signal_mapping, NULL);
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
		__sys_thread_init_complete = true;
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

	thread->block_signals = false;

	simple_ghmap_destroy(&thread->external_tls);
	simple_ghmap_destroy(&thread->signal_handlers);

	sys_object_destroy(object);

	sys_mutex_lock_sigsafe(&global_thread_map_mutex);
	// clear the thread from the global thread map
	// (this frees the memory for the thread object)
	LIBSYS_WUR_IGNORE(simple_ghmap_clear_h(&global_thread_map, thread->id));
	sys_mutex_unlock_sigsafe(&global_thread_map_mutex);
};

ferr_t sys_thread_create(void* stack, size_t stack_size, sys_thread_entry_f entry, void* context, sys_thread_flags_t flags, sys_thread_t** out_thread) {
	ferr_t status = ferr_ok;
	sys_thread_object_t* thread = NULL;
	uintptr_t stack_top;
	bool free_stack_on_fail = false;
	libsyscall_signal_mapping_t signal_mapping;
	sys_thread_id_t thread_id = SYS_THREAD_ID_INVALID;
	bool created = false;
	bool release_on_fail = false;

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

	status = libsyscall_wrapper_thread_create(stack, stack_size, __sys_thread_entry, &thread_id);
	if (status != ferr_ok) {
		goto out;
	}

	sys_mutex_lock_sigsafe(&global_thread_map_mutex);

	status = simple_ghmap_lookup_h(&global_thread_map, thread_id, true, sizeof(*thread), &created, (void*)&thread, NULL);
	if (status != ferr_ok) {
		sys_mutex_unlock_sigsafe(&global_thread_map_mutex);
		goto out;
	}

	if (!created) {
		// this should be impossible
		status = ferr_should_restart;
		sys_mutex_unlock_sigsafe(&global_thread_map_mutex);
		goto out;
	}

	status = sys_object_init(&thread->object, &thread_class);
	if (status != ferr_ok) {
		FERRO_WUR_IGNORE(simple_ghmap_clear_h(&global_thread_map, thread_id));
		sys_mutex_unlock_sigsafe(&global_thread_map_mutex);
		goto out;
	}

	thread->id = thread_id;

	status = simple_ghmap_init(&thread->external_tls, 16, 0, simple_ghmap_allocate_sys_mempool, simple_ghmap_free_sys_mempool, NULL, NULL, NULL, NULL, NULL, NULL);
	if (status != ferr_ok) {
		FERRO_WUR_IGNORE(simple_ghmap_clear_h(&global_thread_map, thread_id));
		sys_mutex_unlock_sigsafe(&global_thread_map_mutex);
		goto out;
	}

	status = simple_ghmap_init(&thread->signal_handlers, 32, 0, simple_ghmap_allocate_sys_mempool, simple_ghmap_free_sys_mempool, NULL, NULL, NULL, NULL, NULL, NULL);
	if (status != ferr_ok) {
		FERRO_WUR_IGNORE(simple_ghmap_destroy(&thread->external_tls));
		FERRO_WUR_IGNORE(simple_ghmap_clear_h(&global_thread_map, thread_id));
		sys_mutex_unlock_sigsafe(&global_thread_map_mutex);
		goto out;
	}

	simple_memset(thread->tls, 0, sizeof(thread->tls));

	thread->tls[sys_thread_tls_key_tls] = &thread->tls[sys_thread_tls_key_tls];
	thread->tls[sys_thread_tls_key_self] = thread;

	thread->free_on_death = free_stack_on_fail ? stack : NULL;
	thread->block_signals = false;
	thread->signal_block_count = 0;

	// set up the stack
	*(void**)(stack_top - 0x08) = entry;
	*(void**)(stack_top - 0x10) = context;
	*(void**)(stack_top - 0x18) = thread;

	signal_mapping.block_all_flag = &thread->block_signals;

	// these are mapped to 0, meaning their default actions (which is killing us) should be taken instead
	signal_mapping.bus_error_signal = 0;
	signal_mapping.page_fault_signal = 0;
	signal_mapping.floating_point_exception_signal = 0;
	signal_mapping.illegal_instruction_signal = 0;
	signal_mapping.debug_signal = 0;

	status = libsyscall_wrapper_thread_signal_update_mapping(thread->id, &signal_mapping, NULL);
	if (status != ferr_ok) {
		FERRO_WUR_IGNORE(simple_ghmap_destroy(&thread->external_tls));
		FERRO_WUR_IGNORE(simple_ghmap_destroy(&thread->signal_handlers));
		FERRO_WUR_IGNORE(simple_ghmap_clear_h(&global_thread_map, thread_id));
		sys_mutex_unlock_sigsafe(&global_thread_map_mutex);
		goto out;
	}

	sys_mutex_unlock_sigsafe(&global_thread_map_mutex);

	release_on_fail = true;

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
		if (thread && release_on_fail) {
			sys_release((void*)thread);
		}
		if (thread_id != SYS_THREAD_ID_INVALID && !release_on_fail) {
			// we only need to kill the thread if we created it but were unable to populate a thread info structure for it
			libsyscall_wrapper_thread_kill(thread_id);
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

	// we block signals until we die
	sys_thread_block_signals((void*)thread);

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

ferr_t sys_thread_signal(sys_thread_t* object, uint64_t signal) {
	sys_thread_object_t* thread = (void*)object;
	return libsyscall_wrapper_thread_signal(thread->id, signal);
};

// DEBUGGING
#include <libsys/console.h>

LIBSYS_NO_RETURN
#if FERRO_ARCH == FERRO_ARCH_x86_64
__attribute__((force_align_arg_pointer))
#endif
static void sys_thread_signal_handler(void* context, libsyscall_signal_info_t* signal_info) {
	sys_thread_object_t* thread = (void*)sys_thread_current();
	sys_thread_t* target_thread = NULL;
	sys_thread_signal_handler_t* handler_ptr;
	sys_thread_signal_handler_t handler;
	sys_thread_signal_info_private_t sys_signal_info;

	sys_signal_info.original = signal_info;
	sys_signal_info.public.flags = signal_info->flags; // TODO: translate these; they're the same for now
	sys_signal_info.public.signal_number = signal_info->signal_number;
	sys_signal_info.public.thread = NULL;
	sys_signal_info.public.thread_context = signal_info->thread_context;
	sys_signal_info.public.data = signal_info->data;

	if (signal_info->thread_id == thread->id) {
		target_thread = (void*)thread;
		// this cannot fail
		sys_abort_status(sys_retain(target_thread));
	} else {
		ferr_t status = ferr_ok;

		sys_mutex_lock_sigsafe(&global_thread_map_mutex);

		status = simple_ghmap_lookup_h(&global_thread_map, signal_info->thread_id, false, 0, NULL, (void*)&target_thread, NULL);

		if (status == ferr_ok) {
			status = sys_retain(target_thread);
		}

		sys_mutex_unlock_sigsafe(&global_thread_map_mutex);

		if (status != ferr_ok) {
			// ignore this signal
			goto out;
		}
	}

	sys_signal_info.public.thread = target_thread;

	sys_thread_block_signals((void*)thread);

	if (simple_ghmap_lookup_h(&thread->signal_handlers, signal_info->signal_number, false, 0, NULL, (void*)&handler_ptr, NULL) == ferr_ok) {
		simple_memcpy(&handler, handler_ptr, sizeof(handler));
	}

	sys_thread_unblock_signals((void*)thread);

	if (handler.handler) {
		handler.handler(handler.context, &sys_signal_info.public);
	}

out:
	if (sys_signal_info.public.thread) {
		sys_release(sys_signal_info.public.thread);
	}

	// TODO: translate these; they're the same for now
	signal_info->flags = sys_signal_info.public.flags;
	signal_info->data = sys_signal_info.public.data;
	signal_info->mask = sys_signal_info.public.mask;

	while (true) {
		ferr_t status = libsyscall_wrapper_thread_signal_return(signal_info);
		if (status != ferr_signaled) {
			sys_abort();
		}
	}
};

ferr_t sys_thread_signal_configure(uint64_t signal, const sys_thread_signal_configuration_t* new_configuration, sys_thread_signal_configuration_t* out_old_configuration) {
	sys_thread_object_t* thread = (void*)sys_thread_current();
	libsyscall_signal_configuration_t new_config;
	libsyscall_signal_configuration_t old_config;
	ferr_t status = ferr_ok;
	bool created = false;
	sys_thread_signal_handler_t* handler = NULL;

	sys_thread_block_signals((void*)thread);

	status = simple_ghmap_lookup_h(&thread->signal_handlers, signal, !!new_configuration, sizeof(*handler), &created, (void*)&handler, NULL);
	if (status != ferr_ok) {
		if (!!new_configuration) {
			goto out;
		} else {
			status = ferr_ok;
			if (out_old_configuration) {
				simple_memset(out_old_configuration, 0, sizeof(*out_old_configuration));
			}
			goto out;
		}
	}

	if (out_old_configuration && created) {
		simple_memset(out_old_configuration, 0, sizeof(*out_old_configuration));
	}

	if (new_configuration) {
		new_config.context = NULL;
		new_config.handler = sys_thread_signal_handler;
		new_config.flags = 0;

		if (new_configuration->flags & sys_thread_signal_configuration_flag_enabled) {
			new_config.flags |= libsyscall_signal_configuration_flag_enabled;
		}

		if (new_configuration->flags & sys_thread_signal_configuration_flag_coalesce) {
			new_config.flags |= libsyscall_signal_configuration_flag_coalesce;
		}

		if (new_configuration->flags & sys_thread_signal_configuration_flag_allow_redirection) {
			new_config.flags |= libsyscall_signal_configuration_flag_allow_redirection;
		}

		if (new_configuration->flags & sys_thread_signal_configuration_flag_preempt) {
			new_config.flags |= libsyscall_signal_configuration_flag_preempt;
		}

		if (new_configuration->flags & sys_thread_signal_configuration_flag_block_on_redirect) {
			new_config.flags |= libsyscall_signal_configuration_flag_block_on_redirect;
		}

		if (new_configuration->flags & sys_thread_signal_configuration_flag_mask_on_handle) {
			new_config.flags |= libsyscall_signal_configuration_flag_mask_on_handle;
		}
	}

	status = libsyscall_wrapper_thread_signal_configure(thread->id, signal, new_configuration ? &new_config : NULL, out_old_configuration ? &old_config : NULL);
	if (status != ferr_ok) {
		goto out;
	}

	if (!created && out_old_configuration) {
		out_old_configuration->handler = handler->handler;
		out_old_configuration->context = handler->context;

		out_old_configuration->flags = 0;

		if (old_config.flags & libsyscall_signal_configuration_flag_enabled) {
			out_old_configuration->flags |= sys_thread_signal_configuration_flag_enabled;
		}

		if (old_config.flags & libsyscall_signal_configuration_flag_coalesce) {
			out_old_configuration->flags |= sys_thread_signal_configuration_flag_coalesce;
		}

		if (old_config.flags & libsyscall_signal_configuration_flag_allow_redirection) {
			out_old_configuration->flags |= sys_thread_signal_configuration_flag_allow_redirection;
		}

		if (old_config.flags & libsyscall_signal_configuration_flag_preempt) {
			out_old_configuration->flags |= sys_thread_signal_configuration_flag_preempt;
		}

		if (old_config.flags & libsyscall_signal_configuration_flag_block_on_redirect) {
			out_old_configuration->flags |= sys_thread_signal_configuration_flag_block_on_redirect;
		}

		if (old_config.flags & libsyscall_signal_configuration_flag_mask_on_handle) {
			out_old_configuration->flags |= sys_thread_signal_configuration_flag_mask_on_handle;
		}
	}

	if (new_configuration) {
		handler->handler = new_configuration->handler;
		handler->context = new_configuration->context;
	}

out:
	if (status != ferr_ok) {
		if (created) {
			FERRO_WUR_IGNORE(simple_ghmap_clear_h(&thread->signal_handlers, signal));
		}
	}
	sys_thread_unblock_signals((void*)thread);
	return status;
};

void sys_thread_block_signals(sys_thread_t* object) {
	sys_thread_object_t* thread = (void*)object;

	if (!thread) {
		return;
	}

	if (__atomic_fetch_add(&thread->signal_block_count, 1, __ATOMIC_RELAXED) == 0) {
		thread->block_signals = true;
	}
};

void sys_thread_unblock_signals(sys_thread_t* object) {
	sys_thread_object_t* thread = (void*)object;

	if (!thread) {
		return;
	}

	if (__atomic_sub_fetch(&thread->signal_block_count, 1, __ATOMIC_RELAXED) == 0) {
		thread->block_signals = false;
	}
};

ferr_t sys_thread_signal_stack_configure(sys_thread_t* object, const sys_thread_signal_stack_t* new_stack, sys_thread_signal_stack_t* out_old_stack) {
	sys_thread_object_t* thread = (void*)object;
	ferr_t status = ferr_ok;
	libsyscall_signal_stack_t libsyscall_new_stack;
	libsyscall_signal_stack_t libsyscall_old_stack;

	if (object != sys_thread_current()) {
		// for now
		status = ferr_invalid_argument;
		goto out;
	}

	if (new_stack) {
		// TODO: translate flags; for now, we can just copy, since they're the same (just with different names)
		libsyscall_new_stack.flags = new_stack->flags;
		libsyscall_new_stack.base = new_stack->base;
		libsyscall_new_stack.size = new_stack->size;
	}

	status = libsyscall_wrapper_thread_signal_stack(new_stack ? &libsyscall_new_stack : NULL, out_old_stack ? &libsyscall_old_stack : NULL);
	if (status != ferr_ok) {
		goto out;
	}

	if (out_old_stack) {
		// TODO: translate flags
		out_old_stack->flags = libsyscall_old_stack.flags;
		out_old_stack->base = libsyscall_old_stack.base;
		out_old_stack->size = libsyscall_old_stack.size;
	}

out:
	return status;
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
