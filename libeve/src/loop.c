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

#include <libeve/loop.private.h>
#include <libeve/item.private.h>
#include <libeve/locks.h>
#include <libsys/locks.private.h>
#include <gen/libsyscall/syscall-wrappers.h>

LIBEVE_STRUCT(eve_futex_suspension_context) {
	sys_event_t suspension_event;
	eve_loop_work_id_t work_id;
};

// maximum number of threads to create eagerly per loop.
//
// eager creation is when a thread is automatically
// created in response to a work item being enqueued
// when all existing threads are busy.
#define DEFAULT_EAGER_THREAD_LIMIT 10

// maximum number of threads per loop.
#define DEFAULT_TOTAL_THREAD_LIMIT 20

// after the maximum number of worker threads have been created eagerly,
// additional worker threads are only created after a (brief) timeout
// with all threads still busy. if a thread becomes available before
// this timeout elapses, a worker thread is not created.
// otherwise, if the timeout expires and no thread has become available,
// a worker thread is created, unless the total thread limit has been
// reached.
//
// this pessimistic thread creation is meant to avoid creating too many
// worker threads and hogging system resources while also providing a
// decent failsafe in case of having too much long-running work items.
//
// 50ms
#define DEFAULT_PESSIMISTIC_WORK_TIMEOUT_NS (50ull * 1000 * 1000)

// time to wait for work to be enqueued in an automatically created
// worker thread before destroying the thread. if no work becomes available
// before this timeout expires, the worker thread is destroyed.
//
// 15s
#define DEFAULT_WORKER_THREAD_TIMEOUT_NS (15ull * 1000 * 1000 * 1000)

static void ensure_tls_key(void);

static sys_tls_key_t loop_tls_key;
static sys_tls_key_t work_tls_key;
static sys_tls_key_t context_tls_key;

static eve_loop_t* main_loop = NULL;
static sys_once_t main_loop_init_token = SYS_ONCE_INITIALIZER;

// 512 KiB
#define STACK_POOL_STACK_SIZE (512ull * 1024)

static sys_mutex_t stack_pool_mutex = SYS_MUTEX_INIT;
static void* stack_pool_saved_stacks[4] = {0};

LIBEVE_WUR static ferr_t stack_pool_allocate(void** out_stack_base, size_t* out_stack_size) {
	ferr_t status = ferr_ok;

	sys_mutex_lock(&stack_pool_mutex);

	for (size_t i = 0; i < sizeof(stack_pool_saved_stacks) / sizeof(*stack_pool_saved_stacks); ++i) {
		if (stack_pool_saved_stacks[i]) {
			*out_stack_base = stack_pool_saved_stacks[i];
			if (out_stack_size) {
				*out_stack_size = STACK_POOL_STACK_SIZE;
			}
			stack_pool_saved_stacks[i] = NULL;
			sys_mutex_unlock(&stack_pool_mutex);
			return status;
		}
	}

	sys_mutex_unlock(&stack_pool_mutex);

	status = sys_page_allocate(sys_page_round_up_count(STACK_POOL_STACK_SIZE), 0, out_stack_base);
	if (status == ferr_ok && out_stack_size) {
		*out_stack_size = STACK_POOL_STACK_SIZE;
	}

	return status;
};

static void stack_pool_free(void* stack_base) {
	sys_mutex_lock(&stack_pool_mutex);

	for (size_t i = 0; i < sizeof(stack_pool_saved_stacks) / sizeof(*stack_pool_saved_stacks); ++i) {
		if (!stack_pool_saved_stacks[i]) {
			stack_pool_saved_stacks[i] = stack_base;
			sys_mutex_unlock(&stack_pool_mutex);
			return;
		}
	}

	sys_mutex_unlock(&stack_pool_mutex);

	LIBEVE_WUR_IGNORE(sys_page_free(stack_base));
};

static void eve_loop_polling_thread(void* context, sys_thread_t* this_thread) {
	eve_loop_object_t* loop = context;
	bool alive = true;

	ensure_tls_key();
	sys_abort_status(sys_tls_set(loop_tls_key, (uintptr_t)loop));

	// FIXME: this can become an infinite loop if the monitor has level-triggered items.
	//        the workaround for now? just don't use level-triggered items.
	//        this can also be a problem if one of the items is edge-triggered but is constantly being triggered.
	while (true) {
		sys_monitor_poll_item_t poll_items[16];
		size_t poll_item_count = sizeof(poll_items) / sizeof(*poll_items);
		ferr_t status = sys_monitor_poll(loop->monitor, 0, 0, alive ? sys_timeout_type_none : sys_timeout_type_relative_ns_monotonic, poll_items, &poll_item_count);

		if (status != ferr_ok) {
			if (!alive && status == ferr_timed_out) {
				// there are no more events left
				break;
			}
			continue;
		}

		for (size_t i = 0; i < poll_item_count; ++i) {
			sys_monitor_poll_item_t* poll_item = &poll_items[i];
			sys_object_t* target = (poll_item->type == sys_monitor_poll_item_type_item) ? sys_monitor_item_target(poll_item->item) : NULL;

			if (target == loop->death_counter) {
				if (sys_counter_value(loop->death_counter) > 0) {
					alive = false;
				}
			} else if (poll_item->type == sys_monitor_poll_item_type_futex) {
				// this is a futex that we scheduled to wake up a work item
				eve_futex_suspension_context_t* context = poll_item->futex_context;
				// this wait is fine; it's guaranteed to be an extremely short wait at worst.
				sys_event_wait(&context->suspension_event);
				if (eve_loop_resume((void*)loop, context->work_id) != ferr_ok) {
					// DEBUG
					sys_console_log_f("*** FAILED TO RESUME WORK ITEM FOR FUTEX ***\n");
				}
			} else if (poll_item->type == sys_monitor_poll_item_type_timeout) {
				// this is a timeout that we scheduled to wake up an item

				// the item we scheduled the timeout for might've been canceled
				// or resumed early, so it's fine if we fail to find it
				LIBEVE_WUR_IGNORE(eve_loop_resume((void*)loop, (uintptr_t)poll_item->timeout_context));
			} else {
				sys_monitor_events_t important_events = poll_item->events & ~sys_monitor_event_item_deleted;
				eve_object_t* item = sys_monitor_item_context(poll_item->item);

				// the only event we still handle once the loop dies is "item deleted" events.
				// this is because those items are no longer present in the item list,
				// so we need the system to tell us what they are.
				if (alive && important_events != 0) {
					const eve_item_interface_t* interface = (const void*)sys_object_interface_find(&item->object_class->interface, sys_object_interface_namespace_libeve, eve_object_interface_type_item);

					if (!interface) {
						sys_abort();
					}

					interface->handle_events(item, important_events);
				}

				if (poll_item->events & sys_monitor_event_item_deleted) {
					// now we can release this item
					// (it should've already been removed from the item list when it was removed from the loop)
					eve_release(item);
				}
			}

			if (poll_item->type == sys_monitor_poll_item_type_item) {
				sys_release(poll_item->item);
			}
		}
	}

	// remove all items from the monitor (and release them)
	// no need to acquire the mutex since the loop must be dead for us to reach this code
	for (size_t i = 0; i < loop->item_count; ++i) {
		eve_object_t* item = loop->items[i];
		const eve_item_interface_t* interface = (const void*)sys_object_interface_find(&item->object_class->interface, sys_object_interface_namespace_libeve, eve_object_interface_type_item);
		sys_monitor_item_t* monitor_item;

		if (!interface) {
			sys_abort();
		}

		monitor_item = interface->get_monitor_item(item);

		if (monitor_item) {
			LIBEVE_WUR_IGNORE(sys_monitor_remove_item(loop->monitor, monitor_item, false));
		}

		eve_release(item);
	}

	// clean up the loop
	if (loop->items) {
		sys_abort_status(sys_mempool_free(loop->items));
	}
	sys_release(loop->death_counter);
	sys_release(loop->monitor);
	if (loop->ring_inited) {
		simple_ring_destroy(&loop->ring);
	}
	if (loop->suspended_work) {
		LIBEVE_WUR_IGNORE(sys_mempool_free(loop->suspended_work));
	}
	sys_object_destroy((void*)loop);
};

static void eve_loop_destroy(eve_object_t* obj) {
	eve_loop_object_t* loop = (void*)obj;

	// wake up the polling thread (if we have one)
	if (loop->death_counter) {
		sys_counter_increment(loop->death_counter);
	}

	if (loop->polling_thread) {
		// it's the polling thread's job to destroy the object
		// (and finish cleaning up)
		sys_release(loop->polling_thread);
	} else {
		// we don't have a polling thread, so it's our job to destroy it
		// (and finish cleaning up)
		if (loop->monitor) {
			sys_release(loop->monitor);
		}
		if (loop->death_counter) {
			sys_release(loop->death_counter);
		}
		if (loop->ring_inited) {
			simple_ring_destroy(&loop->ring);
		}
		if (loop->suspended_work) {
			LIBEVE_WUR_IGNORE(sys_mempool_free(loop->suspended_work));
		}
		sys_object_destroy(obj);
	}
};

const eve_object_class_t eve_loop_class = {
	LIBSYS_OBJECT_CLASS_INTERFACE(NULL),
	.destroy = eve_loop_destroy,
};

const eve_object_class_t* eve_object_class_loop(void) {
	return &eve_loop_class;
};

ferr_t eve_loop_create(eve_loop_t** out_loop) {
	ferr_t status = ferr_ok;
	eve_loop_object_t* loop = NULL;
	sys_monitor_item_t* counter_item = NULL;

	status = sys_object_new(&eve_loop_class, sizeof(*loop) - sizeof(loop->object), (void*)&loop);
	if (status != ferr_ok) {
		goto out;
	}

	simple_memset((char*)loop + sizeof(loop->object), 0, sizeof(*loop) - sizeof(loop->object));

	sys_mutex_init(&loop->mutex);
	sys_semaphore_init(&loop->work_semaphore, 0);
	sys_mutex_init(&loop->suspended_work_mutex);

	status = sys_counter_create(0, &loop->death_counter);
	if (status != ferr_ok) {
		goto out;
	}

	status = simple_ring_init(&loop->ring, sizeof(*loop->ring_buffer), sizeof(loop->ring_buffer) / sizeof(*loop->ring_buffer), loop->ring_buffer, simple_ghmap_allocate_sys_mempool, simple_ghmap_free_sys_mempool, NULL, simple_ring_flag_dynamic);
	if (status != ferr_ok) {
		goto out;
	}

	loop->ring_inited = true;

	status = sys_monitor_create(&loop->monitor);
	if (status != ferr_ok) {
		goto out;
	}

	status = sys_monitor_item_create(loop->death_counter, sys_monitor_item_flag_enabled | sys_monitor_item_flag_active_high | sys_monitor_item_flag_edge_triggered, sys_monitor_event_counter_updated, loop, &counter_item);
	if (status != ferr_ok) {
		goto out;
	}

	status = sys_monitor_add_item(loop->monitor, counter_item);
	if (status != ferr_ok) {
		goto out;
	}

	status = sys_thread_create(NULL, 2ull * 1024 * 1024, eve_loop_polling_thread, loop, sys_thread_flag_resume, &loop->polling_thread);
	if (status != ferr_ok) {
		goto out;
	}

out:
	if (counter_item) {
		sys_release(counter_item);
	}
	if (status == ferr_ok) {
		*out_loop = (void*)loop;
	} else {
		if (loop) {
			sys_release((void*)loop);
		}
	}
	return status;
};

static sys_once_t loop_tls_once = SYS_ONCE_INITIALIZER;

static void loop_tls_init(void* context) {
	sys_abort_status(sys_tls_key_create(NULL, &loop_tls_key));
	sys_abort_status(sys_tls_key_create(NULL, &work_tls_key));
	sys_abort_status(sys_tls_key_create(NULL, &context_tls_key));
};

static void ensure_tls_key(void) {
	sys_once(&loop_tls_once, loop_tls_init, NULL);
};

static void main_loop_init(void* context) {
	sys_abort_status(eve_loop_create(&main_loop));
};

eve_loop_t* eve_loop_get_main(void) {
	sys_once(&main_loop_init_token, main_loop_init, NULL);
	sys_abort_status(eve_retain(main_loop));
	return main_loop;
};

eve_loop_t* eve_loop_get_current(void) {
	ferr_t status = ferr_ok;
	sys_tls_value_t val;
	eve_loop_t* loop = NULL;

	ensure_tls_key();

	status = sys_tls_get(loop_tls_key, &val);
	if (status != ferr_ok) {
		goto out;
	}

	loop = (eve_loop_t*)val;

out:
	return loop;
};

void eve_loop_run(eve_loop_t* obj) {
	// TODO: allow loop to be exited
	while (true) {
		eve_loop_run_one(obj);
	}
};

__attribute__((noreturn))
static void eve_loop_runner(void* context) {
	eve_loop_work_item_t* work_item;
	sys_ucs_context_t* saved_context;

	sys_abort_status(sys_tls_get(work_tls_key, (uintptr_t*)&work_item));

	work_item->work(work_item->context);

	// refresh the work_item variable because the work item may have suspended,
	// which would mean that the memory that work_item points to would be invalidated
	// since it's on the stack of eve_loop_run_one
	sys_abort_status(sys_tls_get(work_tls_key, (uintptr_t*)&work_item));

	sys_abort_status(sys_tls_get(context_tls_key, (uintptr_t*)&saved_context));

	work_item->id = eve_loop_work_id_invalid;
	sys_ucs_switch(saved_context, NULL);
	__builtin_unreachable();
};

void eve_loop_run_one(eve_loop_t* obj) {
	eve_loop_object_t* loop = (void*)obj;
	eve_loop_work_item_t work_items[16];
	size_t count = sizeof(work_items) / sizeof(*work_items);
	uintptr_t prev_tls = 0;
	uintptr_t prev_context = 0;
	uintptr_t prev_work = 0;
	sys_ucs_context_t saved_context;

	sys_semaphore_down(&loop->work_semaphore);

	sys_mutex_lock(&loop->mutex);
	count = simple_ring_dequeue(&loop->ring, work_items, count);
	sys_mutex_unlock(&loop->mutex);

	ensure_tls_key();

	LIBEVE_WUR_IGNORE(sys_tls_get(loop_tls_key, &prev_tls));
	sys_abort_status(sys_tls_set(loop_tls_key, (uintptr_t)loop));

	LIBEVE_WUR_IGNORE(sys_tls_get(context_tls_key, &prev_context));
	sys_abort_status(sys_tls_set(context_tls_key, (uintptr_t)&saved_context));

	LIBEVE_WUR_IGNORE(sys_tls_get(work_tls_key, &prev_work));

	for (size_t i = 0; i < count; ++i) {
		sys_abort_status(sys_tls_set(work_tls_key, (uintptr_t)&work_items[i]));
		if (!work_items[i].stack) {
			size_t stack_size = 0;
			sys_abort_status(stack_pool_allocate(&work_items[i].stack, &stack_size));
			sys_ucs_init_empty(&work_items[i].ucs_context);
			sys_ucs_set_entry(&work_items[i].ucs_context, eve_loop_runner, NULL);
			sys_ucs_set_stack(&work_items[i].ucs_context, work_items[i].stack, stack_size);
		}
		sys_ucs_switch(&work_items[i].ucs_context, &saved_context);
		if (work_items[i].suspension_callback) {
			work_items[i].suspension_callback(work_items[i].suspension_context);
		}
		if (work_items[i].id == eve_loop_work_id_invalid) {
			// the work item has finished
			stack_pool_free(work_items[i].stack);
		}
	}

	sys_abort_status(sys_tls_set(loop_tls_key, prev_tls));
	sys_abort_status(sys_tls_set(context_tls_key, prev_context));
	sys_abort_status(sys_tls_set(work_tls_key, prev_work));
};

ferr_t eve_loop_add_item(eve_loop_t* obj, eve_item_t* item) {
	ferr_t status = ferr_ok;
	eve_loop_object_t* loop = (void*)obj;
	const eve_item_interface_t* interface = (const void*)sys_object_interface_find(&item->object_class->interface, sys_object_interface_namespace_libeve, eve_object_interface_type_item);
	bool remove_from_array = false;
	sys_monitor_item_t* monitor_item;

	if (!interface) {
		status = ferr_invalid_argument;
		goto out;
	}

	if (eve_retain(item) != ferr_ok) {
		status = ferr_permanent_outage;
		item = NULL;
		goto out;
	}

	sys_mutex_lock(&loop->mutex);

	status = sys_mempool_reallocate(loop->items, sizeof(*loop->items) * (loop->item_count + 1), NULL, (void*)&loop->items);
	if (status != ferr_ok) {
		sys_mutex_unlock(&loop->mutex);
		goto out;
	}

	loop->items[loop->item_count] = item;
	++loop->item_count;
	remove_from_array = true;

	sys_mutex_unlock(&loop->mutex);

	monitor_item = interface->get_monitor_item(item);
	if (monitor_item) {
		status = sys_monitor_add_item(loop->monitor, monitor_item);
		if (status != ferr_ok) {
			goto out;
		}
	}

	interface->poll_after_attach(item);

out:
	if (status != ferr_ok) {
		if (remove_from_array) {
			sys_mutex_lock(&loop->mutex);
			for (size_t i = 0; i < loop->item_count; ++i) {
				if (loop->items[i] == item) {
					simple_memmove(&loop->items[i], &loop->items[i + 1], ((loop->item_count - i) - 1) * sizeof(*loop->items));
					--loop->item_count;
					LIBEVE_WUR_IGNORE(sys_mempool_reallocate(loop->items, sizeof(*loop->items) * loop->item_count, NULL, (void*)&loop->items));
					break;
				}
			}
			sys_mutex_unlock(&loop->mutex);
		}
		if (item) {
			eve_release(item);
		}
	}
	return status;
};

ferr_t eve_loop_remove_item(eve_loop_t* obj, eve_item_t* item) {
	ferr_t status = ferr_ok;
	eve_loop_object_t* loop = (void*)obj;
	const eve_item_interface_t* interface = (const void*)sys_object_interface_find(&item->object_class->interface, sys_object_interface_namespace_libeve, eve_object_interface_type_item);
	sys_monitor_item_t* monitor_item;

	if (!interface) {
		status = ferr_invalid_argument;
		goto out;
	}

	monitor_item = interface->get_monitor_item(item);
	if (monitor_item) {
		status = sys_monitor_remove_item(loop->monitor, monitor_item, true);
		if (status != ferr_ok) {
			goto out;
		}
	}

	status = ferr_no_such_resource;
	sys_mutex_lock(&loop->mutex);
	for (size_t i = 0; i < loop->item_count; ++i) {
		if (loop->items[i] == item) {
			simple_memmove(&loop->items[i], &loop->items[i + 1], ((loop->item_count - i) - 1) * sizeof(*loop->items));
			--loop->item_count;
			LIBEVE_WUR_IGNORE(sys_mempool_reallocate(loop->items, sizeof(*loop->items) * loop->item_count, NULL, (void*)&loop->items));
			status = ferr_ok;
			break;
		}
	}
	sys_mutex_unlock(&loop->mutex);

	if (status != ferr_ok) {
		goto out;
	}

	if (!monitor_item) {
		// if the item has a monitor item, it's only released once the monitor item is fully removed from the monitor.
		// otherwise (if it doesn't have a monitor item), we release it here
		eve_release(item);
	}

out:
	return status;
};

ferr_t eve_loop_enqueue(eve_loop_t* obj, eve_loop_work_f work, void* context) {
	ferr_t status = ferr_ok;
	eve_loop_object_t* loop = (void*)obj;
	eve_loop_work_item_t work_item;

	work_item.work = work;
	work_item.context = context;
	work_item.stack = NULL;
	work_item.suspension_callback = NULL;
	work_item.suspension_context = NULL;

retry_id:
	work_item.id = __atomic_fetch_add(&loop->next_id, 1, __ATOMIC_RELAXED);
	if (work_item.id == eve_loop_work_id_invalid) {
		goto retry_id;
	}

	sys_mutex_lock(&loop->mutex);
	if (simple_ring_enqueue(&loop->ring, &work_item, 1) != 1) {
		status = ferr_temporary_outage;
	}
	sys_mutex_unlock(&loop->mutex);

	if (status == ferr_ok) {
		sys_semaphore_up(&loop->work_semaphore);
	}

out:
	return status;
};

ferr_t eve_loop_suspend_current(eve_loop_t* obj, eve_loop_suspension_callback_f suspension_callback, void* context, eve_loop_work_id_t* out_id) {
	ferr_t status = ferr_ok;
	eve_loop_object_t* loop = (void*)obj;
	eve_loop_work_item_t* current = NULL;
	eve_loop_work_item_t* suspended = NULL;
	sys_ucs_context_t* saved_context;

	ensure_tls_key();

	if (sys_tls_get(work_tls_key, (uintptr_t*)&current) != ferr_ok) {
		status = ferr_no_such_resource;
		goto out;
	}

	sys_mutex_lock(&loop->suspended_work_mutex);

	status = sys_mempool_reallocate(loop->suspended_work, sizeof(*loop->suspended_work) * (loop->suspended_work_count + 1), NULL, (void*)&loop->suspended_work);
	if (status == ferr_ok) {
		suspended = &loop->suspended_work[loop->suspended_work_count];
		++loop->suspended_work_count;
		simple_memcpy(suspended, current, sizeof(*suspended));
	}

	sys_mutex_unlock(&loop->suspended_work_mutex);

	if (status != ferr_ok) {
		goto out;
	}

	*out_id = current->id;

	sys_abort_status(sys_tls_get(context_tls_key, (uintptr_t*)&saved_context));

	current->suspension_callback = suspension_callback;
	current->suspension_context = context;

	sys_ucs_switch(saved_context, &suspended->ucs_context);

out:
	return status;
};

ferr_t eve_loop_resume(eve_loop_t* obj, eve_loop_work_id_t id) {
	ferr_t status = ferr_ok;
	eve_loop_object_t* loop = (void*)obj;
	eve_loop_work_item_t work_item;

	status = ferr_no_such_resource;

	sys_mutex_lock(&loop->suspended_work_mutex);

	for (size_t i = 0; i < loop->suspended_work_count; ++i) {
		eve_loop_work_item_t* suspended = &loop->suspended_work[i];
		if (suspended->id == id) {
			simple_memcpy(&work_item, suspended, sizeof(work_item));
			simple_memcpy(&loop->suspended_work[i], &loop->suspended_work[i + 1], sizeof(*loop->suspended_work) * ((loop->suspended_work_count - i) - 1));
			--loop->suspended_work_count;
			// try to shrink the array (no harm if we fail)
			LIBEVE_WUR_IGNORE(sys_mempool_reallocate(loop->suspended_work, sizeof(*loop->suspended_work) * loop->suspended_work_count, NULL, (void*)&loop->suspended_work));
			status = ferr_ok;
			break;
		}
	}

	sys_mutex_unlock(&loop->suspended_work_mutex);

	if (status != ferr_ok) {
		goto out;
	}

	sys_mutex_lock(&loop->mutex);
	if (simple_ring_enqueue(&loop->ring, &work_item, 1) != 1) {
		status = ferr_temporary_outage;
	}
	sys_mutex_unlock(&loop->mutex);

	if (status == ferr_ok) {
		sys_semaphore_up(&loop->work_semaphore);
	}

out:
	return status;
};

void eve_mutex_lock(sys_mutex_t* mutex) {
	eve_loop_object_t* loop = (void*)eve_loop_get_current();
	eve_loop_work_item_t* current = NULL;
	eve_futex_suspension_context_t context;

	LIBEVE_WUR_IGNORE(sys_tls_get(work_tls_key, (uintptr_t*)&current));

	if (!current) {
		return sys_mutex_lock(mutex);
	}

	// we're running in a work item, so let's suspend it
	// (most of this is copied from sys_mutex_lock)

	uint64_t old_state = sys_mutex_state_unlocked;

	if (__atomic_compare_exchange_n(&mutex->internal, &old_state, sys_mutex_state_locked_uncontended, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
		// great, we got the lock quickly
		// (this is the most common case)
		return;
	}

	sys_event_init(&context.suspension_event);

	// otherwise, we have to take the slow-path and wait

	if (old_state != sys_mutex_state_locked_contended) {
		old_state = __atomic_exchange_n(&mutex->internal, sys_mutex_state_locked_contended, __ATOMIC_ACQUIRE);
	}

	while (old_state != sys_mutex_state_unlocked) {
		if (sys_monitor_oneshot_futex(loop->monitor, &mutex->internal, 0, sys_mutex_state_locked_contended, &context) == ferr_ok) {
			if (eve_loop_suspend_current((void*)loop, (void*)sys_event_notify, &context.suspension_event, &context.work_id) != ferr_ok) {
				// DEBUG
				sys_console_log_f("*** FAILED TO SUSPEND WORK ITEM ***\n");
			}
		} else {
			// DEBUG
			sys_console_log_f("*** FAILED TO SETUP ONESHOT FUTEX ***\n");
		}
		old_state = __atomic_exchange_n(&mutex->internal, sys_mutex_state_locked_contended, __ATOMIC_ACQUIRE);
	}
};

void eve_semaphore_down(sys_semaphore_t* semaphore) {
	eve_loop_object_t* loop = (void*)eve_loop_get_current();
	eve_loop_work_item_t* current = NULL;
	eve_futex_suspension_context_t context;

	LIBEVE_WUR_IGNORE(sys_tls_get(work_tls_key, (uintptr_t*)&current));

	if (!current) {
		return sys_semaphore_down(semaphore);
	}

	// we're running in a work item, so let's suspend it
	// (most of this is copied from sys_semaphore_down)

	uint64_t old_state = __atomic_load_n(&semaphore->internal, __ATOMIC_RELAXED);
	bool have_waited = false;

	while (true) {
		uint64_t count = old_state & ~sys_semaphore_state_up_needs_to_wake_bit;

		if (count > 0) {
			// there might a chance for us to decrement

			uint64_t new_up_needs_to_wake_bit = old_state & sys_semaphore_state_up_needs_to_wake_bit;
			bool going_to_wake = false;

			if (have_waited && new_up_needs_to_wake_bit == 0) {
				// if we previously slept and were woken up (i.e. `have_waited`), we're responsible for waking other waiters up.
				// however, we're only responsible for that if the up-needs-to-wake bit is not currently set.
				// if it *is* set, then sys_semaphore_up() is responsible for waking others.
				// additionally, we only need to wake other waiters up if the semaphore can be further decremented.
				if (count > 1) {
					going_to_wake = true;
				}

				// set the up-needs-to-wake bit so that the waiters we're about to wake up don't try to wake others up.
				//
				// also set it so that future sys_semaphore_up() calls will know that they need to wake others up.
				// we're only going to wake as many waiters as the semaphore can currently handle;
				// future sys_semaphore_up() calls may change that and we can't possibly now that now.
				new_up_needs_to_wake_bit = sys_semaphore_state_up_needs_to_wake_bit;
			}

			// try to set the new state (count - 1, possibly with the needs-to-wake bit set)
			if (!__atomic_compare_exchange_n(&semaphore->internal, &old_state, (count - 1) | new_up_needs_to_wake_bit, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
				// if we failed to exchange the new state, something changed;
				// let's loop back around and check the new state
				continue;
			}

			if (going_to_wake) {
				libsyscall_wrapper_futex_wake(&semaphore->internal, 0, count - 1, 0);
			}

			// we've successfully decremented the semaphore
			return;
		}

		if (old_state == 0) {
			// if the old state was 0, the up-needs-to-wake bit was not set.
			// we need to set it now so that future sys_semaphore_up() calls will wake us.
			if (!__atomic_compare_exchange_n(&semaphore->internal, &old_state, sys_semaphore_state_up_needs_to_wake_bit, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
				// if we failed to exchange, let's loop around and reevaluate the state
				continue;
			}
		}

		if (sys_monitor_oneshot_futex(loop->monitor, &semaphore->internal, 0, sys_semaphore_state_up_needs_to_wake_bit, &context) == ferr_ok) {
			if (eve_loop_suspend_current((void*)loop, (void*)sys_event_notify, &context.suspension_event, &context.work_id) != ferr_ok) {
				// DEBUG
				sys_console_log_f("*** SEMAPHORE: FAILED TO SUSPEND WORK ITEM ***\n");
			}
		} else {
			// DEBUG
			sys_console_log_f("*** SEMAPHORE: FAILED TO SETUP ONESHOT FUTEX ***\n");
		}

		have_waited = true;

		// this is most likely the state we'll see upon reevaluation
		old_state = 1;

		// it's a good guess, but it doesn't matter if it's wrong;
		// we'll get the real value when we try to decrement
	}
};

void eve_event_wait(sys_event_t* event) {
	eve_loop_object_t* loop = (void*)eve_loop_get_current();
	eve_loop_work_item_t* current = NULL;
	eve_futex_suspension_context_t context;

	LIBEVE_WUR_IGNORE(sys_tls_get(work_tls_key, (uintptr_t*)&current));

	if (!current) {
		return sys_event_wait(event);
	}

	// we're running in a work item, so let's suspend it
	// (most of this is copied from sys_event_wait)

	uint64_t old_state = sys_event_state_unset_no_wait;

	if (__atomic_compare_exchange_n(&event->internal, &old_state, sys_event_state_unset_wait, false, __ATOMIC_ACQUIRE, __ATOMIC_ACQUIRE)) {
		// if we succeeded in setting it to "unset_wait", update our stored `old_state` to match
		old_state = sys_event_state_unset_wait;
	}

	while (old_state != sys_event_state_set) {
		if (sys_monitor_oneshot_futex(loop->monitor, &event->internal, 0, old_state, &context) == ferr_ok) {
			if (eve_loop_suspend_current((void*)loop, (void*)sys_event_notify, &context.suspension_event, &context.work_id) != ferr_ok) {
				// DEBUG
				sys_console_log_f("*** EVENT: FAILED TO SUSPEND WORK ITEM ***\n");
			}
		} else {
			// DEBUG
			sys_console_log_f("*** EVENT: FAILED TO SETUP ONESHOT FUTEX ***\n");
		}
		old_state = __atomic_load_n(&event->internal, __ATOMIC_ACQUIRE);
	}
};

ferr_t eve_loop_schedule(eve_loop_t* obj, eve_loop_work_f work, void* context, uint64_t timeout, sys_timeout_type_t timeout_type, eve_loop_work_id_t* out_id) {
	ferr_t status = ferr_ok;
	eve_loop_object_t* loop = (void*)obj;
	eve_loop_work_item_t work_item;
	bool unqueue = false;

	work_item.work = work;
	work_item.context = context;
	work_item.stack = NULL;
	work_item.suspension_callback = NULL;
	work_item.suspension_context = NULL;

retry_id:
	work_item.id = __atomic_fetch_add(&loop->next_id, 1, __ATOMIC_RELAXED);
	if (work_item.id == eve_loop_work_id_invalid) {
		goto retry_id;
	}

	sys_mutex_lock(&loop->suspended_work_mutex);

	status = sys_mempool_reallocate(loop->suspended_work, sizeof(*loop->suspended_work) * (loop->suspended_work_count + 1), NULL, (void*)&loop->suspended_work);
	if (status == ferr_ok) {
		eve_loop_work_item_t* suspended = &loop->suspended_work[loop->suspended_work_count];
		++loop->suspended_work_count;
		simple_memcpy(suspended, &work_item, sizeof(*suspended));
	}

	sys_mutex_unlock(&loop->suspended_work_mutex);

	unqueue = true;

	status = sys_monitor_oneshot_timeout(loop->monitor, timeout, timeout_type, (void*)work_item.id);

out:
	if (status == ferr_ok) {
		if (out_id) {
			*out_id = work_item.id;
		}
	} else if (unqueue) {
		sys_mutex_lock(&loop->suspended_work_mutex);

		for (size_t i = 0; i < loop->suspended_work_count; ++i) {
			eve_loop_work_item_t* suspended = &loop->suspended_work[i];
			if (suspended->id == work_item.id) {
				simple_memcpy(&loop->suspended_work[i], &loop->suspended_work[i + 1], sizeof(*loop->suspended_work) * ((loop->suspended_work_count - i) - 1));
				--loop->suspended_work_count;
				// try to shrink the array (no harm if we fail)
				LIBEVE_WUR_IGNORE(sys_mempool_reallocate(loop->suspended_work, sizeof(*loop->suspended_work) * loop->suspended_work_count, NULL, (void*)&loop->suspended_work));
				break;
			}
		}

		sys_mutex_unlock(&loop->suspended_work_mutex);
	}
	return status;
};

ferr_t eve_loop_cancel(eve_loop_t* obj, eve_loop_work_id_t id) {
	ferr_t status = ferr_no_such_resource;
	eve_loop_object_t* loop = (void*)obj;
	eve_loop_work_item_t work_item;

	sys_mutex_lock(&loop->suspended_work_mutex);

	for (size_t i = 0; i < loop->suspended_work_count; ++i) {
		eve_loop_work_item_t* suspended = &loop->suspended_work[i];
		if (suspended->id == id) {
			simple_memcpy(&work_item, suspended, sizeof(work_item));
			simple_memcpy(&loop->suspended_work[i], &loop->suspended_work[i + 1], sizeof(*loop->suspended_work) * ((loop->suspended_work_count - i) - 1));
			--loop->suspended_work_count;
			// try to shrink the array (no harm if we fail)
			LIBEVE_WUR_IGNORE(sys_mempool_reallocate(loop->suspended_work, sizeof(*loop->suspended_work) * loop->suspended_work_count, NULL, (void*)&loop->suspended_work));
			status = ferr_ok;
			break;
		}
	}

	sys_mutex_unlock(&loop->suspended_work_mutex);

	if (status != ferr_ok) {
		goto out;
	}

	// clean up resources

	if (work_item.stack) {
		stack_pool_free(work_item.stack);
	}

out:
	return status;
};
