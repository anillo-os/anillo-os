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

#include <ferro/userspace/threads.private.h>
#include <ferro/core/threads.private.h>
#include <ferro/core/panic.h>
#include <ferro/core/mempool.h>
#include <ferro/core/ghmap.h>
#include <ferro/core/locks.h>
#include <ferro/core/workers.h>
#include <libsimple/libsimple.h>

// DA7A == Data
// (because the hook is only used to swap address spaces)
#define UTHREAD_HOOK_OWNER_ID (0xDA7Aull)

// uses a thread pointer as the key (and key_size can be anything; we don't use it)
static simple_ghmap_t uthread_map;

// TODO: this should probably be an rwlock instead (once we get those)
static flock_mutex_t uthread_map_mutex = FLOCK_MUTEX_INIT;

static simple_ghmap_hash_t simple_ghmap_hash_thread(void* context, const void* key, size_t key_size) {
	// we can use the thread's pointer as its hash key
	return (uintptr_t)key;
};

futhread_data_t* futhread_data_for_thread(fthread_t* thread) {
	fthread_private_t* private_thread = (void*)thread;
	futhread_data_t* data = NULL;
	uint8_t slot = fthread_find_hook(thread, UTHREAD_HOOK_OWNER_ID);

	if (slot == UINT8_MAX) {
		data = NULL;
	} else {
		data = private_thread->hooks[slot].context;
	}

	return data;
};

void futhread_init(void) {
	fpanic_status(simple_ghmap_init(&uthread_map, 0, sizeof(futhread_data_private_t), simple_ghmap_allocate_mempool, simple_ghmap_free_mempool, simple_ghmap_hash_thread, NULL));

	futhread_arch_init();
};

static void uthread_thread_died(void* context) {
	fthread_t* thread = context;
	futhread_data_t* data = futhread_data_for_thread(thread);

	// we're guaranteed to be called in a thread context, so we can operate normally here

	if (!data) {
		// huh, it's not there. oh well.
		return;
	}

	if ((data->flags & futhread_flag_deallocate_user_stack_on_exit) != 0) {
		fpanic_status(fpage_space_free(data->user_space, data->user_stack_base, fpage_round_up_to_page_count(data->user_stack_size)));
	}

	if ((data->flags & futhread_flag_destroy_address_space_on_exit) != 0) {
		fpage_space_destroy(data->user_space);
	}

	if ((data->flags & futhread_flag_deallocate_address_space_on_exit) != 0) {
		fpanic_status(fmempool_free(data->user_space));
	}

	fwaitq_wake_many(&data->death_wait, SIZE_MAX);
};

static void uthread_thread_destroyed(void* context) {
	fthread_t* thread = context;
	futhread_data_t* data = futhread_data_for_thread(thread);

	fwaitq_wake_many(&data->destroy_wait, SIZE_MAX);

	flock_mutex_lock(&uthread_map_mutex);
	fpanic_status(simple_ghmap_clear(&uthread_map, thread, 0));
	flock_mutex_unlock(&uthread_map_mutex);
};

static ferr_t uthread_ending_interrupt(void* context, fthread_t* thread) {
	futhread_data_t* data = context;
	fpanic_status(fpage_space_swap(data->user_space));
	futhread_ending_interrupt_arch(thread, data);
	return ferr_ok;
};

ferr_t futhread_register(fthread_t* thread, size_t user_stack_size, fpage_space_t* user_space, futhread_flags_t flags, futhread_syscall_handler_f syscall_handler, void* syscall_handler_context) {
	futhread_data_t* data = NULL;
	futhread_data_private_t* private_data = NULL;
	bool created = false;
	bool clear_uthread_on_fail = false;
	bool deallocate_space_on_fail = false;
	bool destroy_space_on_fail = false;
	bool release_stack_on_fail = false;
	bool clear_flag_on_fail = false;
	ferr_t status = ferr_ok;
	void* user_stack_base = NULL;
	fthread_private_t* private_thread = (void*)thread;

retry_lookup:
	if (fthread_is_uthread(thread)) {
		status = ferr_already_in_progress;
		goto out_unlocked;
	}

	flock_mutex_lock(&uthread_map_mutex);

	if (simple_ghmap_lookup(&uthread_map, thread, 0, true, &created, (void*)&data) != ferr_ok) {
		status = ferr_temporary_outage;
		goto out_locked;
	}

	private_data = (void*)data;

	if (!created) {
		// if this happens, it means the new thread has the same address as an old uthread that hasn't been cleared from the hashmap yet.
		// just try again until we're good.
		flock_mutex_unlock(&uthread_map_mutex);
		goto retry_lookup;
	}

	private_data->process = NULL;

	clear_uthread_on_fail = true;

	if (!user_space) {
		if (fmempool_allocate(sizeof(fpage_space_t), NULL, (void*)&user_space) != ferr_ok) {
			status = ferr_temporary_outage;
			goto out_locked;
		}
		deallocate_space_on_fail = true;
		flags |= futhread_flag_deallocate_address_space_on_exit;

		if (fpage_space_init(user_space) != ferr_ok) {
			status = ferr_temporary_outage;
			goto out_locked;
		}
		destroy_space_on_fail = true;
		flags |= futhread_flag_destroy_address_space_on_exit;
	}

	data->user_space = user_space;

	if (fpage_space_allocate(data->user_space, fpage_round_up_to_page_count(user_stack_size), &user_stack_base, fpage_flag_unprivileged) != ferr_ok) {
		status = ferr_temporary_outage;
		goto out_locked;
	}

	release_stack_on_fail = true;
	flags |= futhread_flag_deallocate_user_stack_on_exit;

	data->flags = flags;
	data->user_stack_base = user_stack_base;
	data->user_stack_size = user_stack_size;

	// register a waiter to clear the uthread data when the thread dies
	fwaitq_waiter_init(&data->thread_death_waiter, uthread_thread_died, thread);
	fwaitq_wait(&thread->death_wait, &data->thread_death_waiter);

	fwaitq_waiter_init(&data->thread_destruction_waiter, uthread_thread_destroyed, thread);
	fwaitq_wait(&thread->destroy_wait, &data->thread_destruction_waiter);

	fwaitq_init(&data->death_wait);
	fwaitq_init(&data->destroy_wait);

	flock_spin_intsafe_lock(&thread->lock);
	thread->flags |= fthread_private_flag_has_userspace;
	clear_flag_on_fail = true;
	flock_spin_intsafe_unlock(&thread->lock);

	if (fthread_register_hook(thread, UTHREAD_HOOK_OWNER_ID, data, NULL, NULL, NULL, NULL, uthread_ending_interrupt) == UINT8_MAX) {
		status = ferr_temporary_outage;
		goto out_locked;
	}

	simple_memset(&data->saved_syscall_context, 0, sizeof(data->saved_syscall_context));

	data->syscall_handler = syscall_handler;
	data->syscall_handler_context = syscall_handler_context;

out_locked:
	if (status != ferr_ok) {
		if (release_stack_on_fail) {
			fpanic_status(fpage_space_free(data->user_space, user_stack_base, fpage_round_up_to_page_count(user_stack_size)));
		}
		if (destroy_space_on_fail) {
			fpage_space_destroy(user_space);
		}
		if (deallocate_space_on_fail) {
			FERRO_WUR_IGNORE(fmempool_free(user_space));
		}
		if (clear_uthread_on_fail) {
			fpanic_status(simple_ghmap_clear(&uthread_map, thread, 0));
		}
		if (clear_flag_on_fail) {
			flock_spin_intsafe_lock(&thread->lock);
			thread->flags &= ~fthread_private_flag_has_userspace;
			flock_spin_intsafe_unlock(&thread->lock);
		}
	} else if (thread == fthread_current()) {
		fpanic_status(fpage_space_swap(data->user_space));
	}

	flock_mutex_unlock(&uthread_map_mutex);

out_unlocked:
	return status;
};

ferr_t futhread_jump_user(fthread_t* uthread, void* address) {
	futhread_data_t* data = NULL;

	if (!uthread || !address) {
		return ferr_invalid_argument;
	}

	data = futhread_data_for_thread(uthread);

	if (!data) {
		return ferr_invalid_argument;
	}

	// make sure the address is valid
	// TODO: make sure it's executable and unprivileged
	if (fpage_space_virtual_to_physical(data->user_space, (uintptr_t)address) == UINTPTR_MAX) {
		return ferr_invalid_argument;
	}

	if (uthread == futhread_current()) {
		futhread_jump_user_self_arch(uthread, data, address);
	} else {
		// TODO: support threads other than the current one
		return ferr_unsupported;
	}
};

void futhread_jump_user_self(void* address) {
	fpanic_status(futhread_jump_user(futhread_current(), address));
	__builtin_unreachable();
};

ferr_t futhread_space(fthread_t* uthread, fpage_space_t** out_space) {
	futhread_data_t* data = NULL;

	if (!uthread || !out_space) {
		return ferr_invalid_argument;
	}

	data = futhread_data_for_thread(uthread);

	if (!data) {
		return ferr_invalid_argument;
	}

	*out_space = data->user_space;

	return ferr_ok;
};

ferr_t futhread_context(fthread_t* uthread, fthread_saved_context_t** out_saved_user_context) {
	futhread_data_t* data = NULL;

	if (!uthread || !out_saved_user_context) {
		return ferr_invalid_argument;
	}

	data = futhread_data_for_thread(uthread);

	if (!data) {
		return ferr_invalid_argument;
	}

	*out_saved_user_context = &data->saved_syscall_context;

	return ferr_ok;
};

bool fthread_is_uthread(fthread_t* thread) {
	bool result = false;
	flock_spin_intsafe_lock(&thread->lock);
	result = (thread->flags & fthread_private_flag_has_userspace) != 0;
	flock_spin_intsafe_unlock(&thread->lock);
	return result;
};

fthread_t* futhread_current(void) {
	fthread_t* current = fthread_current();
	return fthread_is_uthread(current) ? current : NULL;
};
