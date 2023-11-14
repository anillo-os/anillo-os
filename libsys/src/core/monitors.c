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

#include <libsys/monitors.private.h>
#include <libsys/channels.private.h>
#include <libsys/mempool.h>
#include <libsimple/libsimple.h>
#include <libsys/timeout.private.h>
#include <libsys/abort.h>
#include <libsys/counters.private.h>

static void sys_monitor_destroy(sys_object_t* obj) {
	sys_monitor_object_t* monitor = (void*)obj;

	// we shouldn't have any items attached at this point
	// ...except maybe dead items that we didn't get a chance to poll.

	if (monitor->monitor_did != SYS_MONITOR_DID_INVALID) {
		libsyscall_wrapper_monitor_close(monitor->monitor_did);
	}

	for (size_t i = monitor->item_count; i < monitor->array_size; ++i) {
		sys_release(monitor->items[i]);
	}
};

static void sys_monitor_item_destroy(sys_object_t* obj) {
	sys_monitor_item_object_t* item = (void*)obj;

	// we shouldn't be attached to any monitors at this point

	sys_release(item->target);
};

static const sys_object_class_t monitor_class = {
	LIBSYS_OBJECT_CLASS_INTERFACE(NULL),
	.destroy = sys_monitor_destroy,
};

LIBSYS_OBJECT_CLASS_GETTER(monitor, monitor_class);

static const sys_object_class_t monitor_item_class = {
	LIBSYS_OBJECT_CLASS_INTERFACE(NULL),
	.destroy = sys_monitor_item_destroy,
};

LIBSYS_OBJECT_CLASS_GETTER(monitor_item, monitor_item_class);

ferr_t sys_monitor_create(sys_monitor_t** out_monitor) {
	ferr_t status = ferr_ok;
	sys_monitor_object_t* monitor = NULL;

	status = sys_object_new(&monitor_class, sizeof(*monitor) - sizeof(sys_object_t), (void*)&monitor);
	if (status != ferr_ok) {
		goto out;
	}

	sys_mutex_init(&monitor->mutex);
	monitor->monitor_did = SYS_MONITOR_DID_INVALID;
	monitor->items = NULL;
	monitor->item_count = 0;
	monitor->array_size = 0;
	monitor->outstanding_polls = 0;

	status = libsyscall_wrapper_monitor_create(&monitor->monitor_did);
	if (status != ferr_ok) {
		goto out;
	}

out:
	if (status == ferr_ok) {
		*out_monitor = (void*)monitor;
	} else {
		if (monitor) {
			sys_release((void*)monitor);
		}
	}
	return status;
};

ferr_t sys_monitor_item_create(sys_object_t* object, sys_monitor_item_flags_t flags, sys_monitor_events_t events, void* context, sys_monitor_item_t** out_item) {
	ferr_t status = ferr_ok;
	sys_monitor_item_object_t* item = NULL;
	const sys_object_class_t* obj_class = sys_object_class(object);

	if (obj_class != sys_object_class_channel() && obj_class != sys_object_class_counter()) {
		status = ferr_invalid_argument;
		goto out;
	}

	if (obj_class == sys_object_class_counter()) {
		// counters must be edge-triggered and active-high
		if ((flags & sys_monitor_item_flag_edge_triggered) == 0 || (flags & sys_monitor_item_flag_active_low) != 0) {
			status = ferr_invalid_argument;
			goto out;
		}
	}

	status = sys_object_new(&monitor_item_class, sizeof(*item) - sizeof(sys_object_t), (void*)&item);
	if (status != ferr_ok) {
		goto out;
	}

	status = sys_retain(object);
	if (status != ferr_ok) {
		goto out;
	}

	sys_mutex_init(&item->mutex);
	item->target = object;
	item->id = UINT64_MAX;
	item->monitor = NULL;
	item->flags = flags;
	item->events = events;
	item->context = context;

out:
	if (status == ferr_ok) {
		*out_item = (void*)item;
	} else {
		if (item) {
			if (item->target) {
				sys_release(item->target);
			}

			sys_release((void*)item);
		}
	}
	return status;
};

ferr_t sys_monitor_item_modify(sys_monitor_item_t* obj, sys_monitor_item_flags_t flags, sys_monitor_events_t events, void* context, void** out_old_context) {
	sys_monitor_item_object_t* item = (void*)obj;
	ferr_t status = ferr_ok;
	sys_monitor_t* monitor = NULL;
	libsyscall_monitor_item_id_t item_id = libsyscall_monitor_item_id_none;

	sys_mutex_lock(&item->mutex);
	item->flags = flags;
	item->events = events;
	if (out_old_context) {
		*out_old_context = item->context;
	}
	item->context = context;
	monitor = item->monitor;
	if (monitor) {
		if (sys_retain(monitor) != ferr_ok) {
			monitor = NULL;
		}
	}
	item_id = item->id;
	sys_mutex_unlock(&item->mutex);

	if (monitor) {
		sys_monitor_object_t* monitor_object = (void*)monitor;
		libsyscall_monitor_update_item_t update_item;
		uint64_t update_item_count = 1;

		update_item.header.id = item_id;
		update_item.header.descriptor_id = sys_monitor_item_descriptor_id(item);
		update_item.header.type = sys_monitor_item_type(item);
		update_item.header.context = (uintptr_t)item;

		// we always request the "item deleted" event so we know when it's safe to release an item
		update_item.events = sys_monitor_events_to_libsyscall_monitor_events(events) | libsyscall_monitor_event_item_deleted;
		update_item.flags = libsyscall_monitor_update_item_flag_update | libsyscall_monitor_update_item_flag_strict_match | sys_monitor_item_flags_to_libsyscall_monitor_update_item_flags(flags);
		update_item.status = ferr_ok;

		if (sys_object_class(item->target) == sys_object_class_counter()) {
			update_item.flags |= libsyscall_monitor_update_item_flag_disable_on_trigger;
			update_item.data1 = __atomic_or_fetch(&((sys_counter_object_t*)item->target)->value, sys_counter_flag_need_to_wake, __ATOMIC_RELAXED);
		}

		sys_mutex_lock(&monitor_object->mutex);
		status = libsyscall_wrapper_monitor_update(monitor_object->monitor_did, 0, &update_item, &update_item_count);
		sys_mutex_unlock(&monitor_object->mutex);

		if (status == ferr_ok) {
			status = update_item.status;
		}

		sys_release(monitor);
	}

	return status;
};

sys_object_t* sys_monitor_item_target(sys_monitor_item_t* obj) {
	sys_monitor_item_object_t* item = (void*)obj;
	return item->target;
};

void* sys_monitor_item_context(sys_monitor_item_t* obj) {
	sys_monitor_item_object_t* item = (void*)obj;
	return item->context;
};

void sys_monitor_item_remove_from_all(sys_monitor_item_t* obj, bool defer_deletion) {
	sys_monitor_item_object_t* item = (void*)obj;
	sys_monitor_t* monitor = NULL;

	sys_mutex_lock(&item->mutex);
	monitor = item->monitor;
	if (monitor) {
		if (sys_retain(monitor) != ferr_ok) {
			monitor = NULL;
		}
	}
	sys_mutex_unlock(&item->mutex);

	if (monitor) {
		LIBSYS_WUR_IGNORE(sys_monitor_remove_item(monitor, obj, defer_deletion));
		sys_release(monitor);
	}
};

ferr_t sys_monitor_add_item(sys_monitor_t* obj, sys_monitor_item_t* item_obj) {
	sys_monitor_object_t* monitor = (void*)obj;
	sys_monitor_item_object_t* item = (void*)item_obj;
	ferr_t status = ferr_ok;
	bool unset_monitor = false;
	bool remove_from_array = false;
	libsyscall_monitor_update_item_t update_item;
	uint64_t update_item_count = 1;

	if (sys_retain(obj) != ferr_ok) {
		obj = NULL;
		status = ferr_permanent_outage;
		goto out;
	}

	if (sys_retain(item_obj) != ferr_ok) {
		item_obj = NULL;
		status = ferr_permanent_outage;
		goto out;
	}

	sys_mutex_lock(&item->mutex);
	if (item->monitor) {
		// TODO: support adding a single item to multiple monitors
		status = ferr_resource_unavailable;
	} else {
		item->monitor = obj;
		item->id = libsyscall_monitor_item_id_none;

		update_item.header.context = (uintptr_t)item;

		// see sys_monitor_item_modify() for why we always request "item deleted" events
		update_item.events = sys_monitor_events_to_libsyscall_monitor_events(item->events) | libsyscall_monitor_event_item_deleted;
		update_item.flags = libsyscall_monitor_update_item_flag_create | sys_monitor_item_flags_to_libsyscall_monitor_update_item_flags(item->flags);
	}
	sys_mutex_unlock(&item->mutex);

	if (status != ferr_ok) {
		goto out;
	}

	if (sys_object_class(item->target) == sys_object_class_counter()) {
		update_item.flags |= libsyscall_monitor_update_item_flag_disable_on_trigger;
		update_item.data1 = 0; // futex channel 0
		update_item.data2 = __atomic_or_fetch(&((sys_counter_object_t*)item->target)->value, sys_counter_flag_need_to_wake, __ATOMIC_RELAXED);
	}

	unset_monitor = true;

	sys_mutex_lock(&monitor->mutex);
	status = sys_mempool_reallocate(monitor->items, (monitor->array_size + 1) * sizeof(*monitor->items), NULL, (void*)&monitor->items);
	if (status == ferr_ok) {
		simple_memmove(&monitor->items[monitor->item_count + 1], &monitor->items[monitor->item_count], (monitor->array_size - monitor->item_count) * sizeof(*monitor->items));
		monitor->items[monitor->item_count] = item_obj;
		++monitor->item_count;
		++monitor->array_size;
	}
	sys_mutex_unlock(&monitor->mutex);

	if (status != ferr_ok) {
		goto out;
	}

	remove_from_array = true;

	update_item.header.id = libsyscall_monitor_item_id_none;
	update_item.header.descriptor_id = sys_monitor_item_descriptor_id(item);
	update_item.header.type = sys_monitor_item_type(item);

	update_item.status = ferr_ok;

	sys_mutex_lock(&monitor->mutex);
	status = libsyscall_wrapper_monitor_update(monitor->monitor_did, 0, &update_item, &update_item_count);
	sys_mutex_unlock(&monitor->mutex);

	if (status != ferr_ok) {
		goto out;
	}

	status = update_item.status;

	if (status != ferr_ok) {
		goto out;
	}

	sys_mutex_lock(&item->mutex);
	item->id = update_item.header.id;
	sys_mutex_unlock(&item->mutex);

out:
	if (status != ferr_ok) {
		if (remove_from_array) {
			sys_mutex_lock(&monitor->mutex);
			for (size_t i = 0; i < monitor->item_count; ++i) {
				if (monitor->items[i] == item_obj) {
					simple_memmove(&monitor->items[i], &monitor->items[i + 1], ((monitor->array_size - i) - 1) * sizeof(*monitor->items));
					--monitor->item_count;
					--monitor->array_size;
					// try to shrink the array, but ignore failure
					LIBSYS_WUR_IGNORE(sys_mempool_reallocate(monitor->items, monitor->array_size * sizeof(*monitor->items), NULL, (void*)&monitor->items));
					break;
				}
			}
			sys_mutex_unlock(&monitor->mutex);
		}
		if (unset_monitor) {
			sys_mutex_lock(&item->mutex);
			item->monitor = NULL;
			sys_mutex_unlock(&item->mutex);
		}
		if (item_obj) {
			sys_release(item_obj);
		}
		if (obj) {
			sys_release(obj);
		}
	}
	return status;
};

ferr_t sys_monitor_remove_item(sys_monitor_t* obj, sys_monitor_item_t* item_obj, bool defer_deletion) {
	sys_monitor_object_t* monitor = (void*)obj;
	sys_monitor_item_object_t* item = (void*)item_obj;
	ferr_t status = ferr_ok;
	libsyscall_monitor_update_item_t update_item;
	uint64_t update_item_count = 1;
	bool can_release_item = false;
	bool can_release_monitor = false;

	sys_mutex_lock(&item->mutex);
	if (item->monitor != (void*)monitor) {
		status = ferr_invalid_argument;
	}
	update_item.header.id = item->id;
	sys_mutex_unlock(&item->mutex);

	if (status != ferr_ok) {
		goto out;
	}

	update_item.header.descriptor_id = sys_monitor_item_descriptor_id(item);
	update_item.header.type = sys_monitor_item_type(item);
	update_item.header.context = (uintptr_t)item;

	sys_mutex_lock(&monitor->mutex);

	if (monitor->outstanding_polls > 0) {
		// always defer deletion if we're currently polling.
		// this way, we avoid a race between our poll function performing the poll syscall
		// and this function deleting the item with the update syscall.
		//
		// without this, it's possible that the poll function marks itself as outstanding,
		// then another thread calls this function but does not use the "defer deletion" flag
		// and the update syscall is performed and the item is deleted, and then finally the original
		// thread performs the poll syscall. in this case, the item would be leaked because, in the kernel's view,
		// we weren't polling when we deleted the item, so it didn't need to generate an event, while in our view,
		// we were polling when we deleted the item, so we didn't need to delete the item now.
		//
		// with this flag always enabled for outstanding polls, the worst case scenario is that the poll function
		// has just finished performing a poll syscall but it's still marked as outstanding. in this case, the item
		// deletion would be deferred until the next poll occurs. not great, but at least we avoid leaking the item entirely.
		defer_deletion = true;
	}

	update_item.flags = libsyscall_monitor_update_item_flag_delete | libsyscall_monitor_update_item_flag_strict_match | (defer_deletion ? libsyscall_monitor_update_item_flag_defer_delete : 0);
	update_item.status = ferr_ok;

	status = libsyscall_wrapper_monitor_update(monitor->monitor_did, 0, &update_item, &update_item_count);

	if (status != ferr_ok) {
		sys_mutex_unlock(&monitor->mutex);
		goto out;
	}

	status = update_item.status;

	if (status != ferr_ok) {
		sys_mutex_unlock(&monitor->mutex);
		goto out;
	}

	for (size_t i = 0; i < monitor->item_count; ++i) {
		if (monitor->items[i] == item_obj) {
			simple_memmove(&monitor->items[i], &monitor->items[i + 1], ((monitor->array_size - i) - 1) * sizeof(*monitor->items));
			--monitor->item_count;

			// we always release the monitor, regardless of whether we're deferring deletion or not.
			// this is because we always remove the reference that the item has on the monitor,
			// so there's no danger of the monitor being accessed that way.
			// this avoids a leak
			can_release_monitor = true;

			if (defer_deletion) {
				// mark the item as dead, but don't release it.
				// that's a job for whichever poll receives the "item deleted" event
				monitor->items[monitor->array_size - 1] = item_obj;
			} else {
				can_release_item = true;
				--monitor->array_size;
				// try to shrink the array, but ignore failure
				LIBSYS_WUR_IGNORE(sys_mempool_reallocate(monitor->items, monitor->array_size * sizeof(*monitor->items), NULL, (void*)&monitor->items));
			}
			break;
		}
	}

	sys_mutex_unlock(&monitor->mutex);

	sys_mutex_lock(&item->mutex);
	item->monitor = NULL;
	sys_mutex_unlock(&item->mutex);

	if (can_release_item) {
		sys_release(item_obj);
	}

	if (can_release_monitor) {
		sys_release(obj);
	}

out:
	return status;
};

ferr_t sys_monitor_poll(sys_monitor_t* obj, sys_monitor_poll_flags_t flags, uint64_t timeout, sys_timeout_type_t timeout_type, sys_monitor_poll_item_t* out_items, size_t* in_out_item_count) {
	sys_monitor_object_t* monitor = (void*)obj;
	ferr_t status = ferr_ok;
	libsyscall_monitor_event_t events[16]; // TODO: maybe create this dynamically on the heap, possibly based on how many item the user wants
	uint64_t event_count = sizeof(events) / sizeof(*events);
	uint64_t out_item_count = 0;

	if (event_count > *in_out_item_count) {
		event_count = *in_out_item_count;
	}

	if (sys_retain(obj) != ferr_ok) {
		obj = NULL;
		status = ferr_permanent_outage;
		goto out;
	}

	sys_mutex_lock(&monitor->mutex);
	++monitor->outstanding_polls;
	sys_mutex_unlock(&monitor->mutex);

	status = libsyscall_wrapper_monitor_poll(monitor->monitor_did, 0, timeout, sys_timeout_type_to_libsyscall_timeout_type(timeout_type), events, &event_count);
	if (status != ferr_ok) {
		goto out;
	}

	for (size_t i = 0; i < event_count; ++i) {
		libsyscall_monitor_event_t* event = &events[i];
		sys_monitor_poll_item_t* poll_item = &out_items[out_item_count];
		bool context_is_item = (event->flags & libsyscall_monitor_event_flag_user) == 0;
		sys_monitor_item_t* item_obj = context_is_item ? (void*)event->header.context : NULL;
		sys_monitor_item_object_t* item = (void*)item_obj;
		sys_monitor_events_t monitored_and_triggered = 0;
		sys_monitor_item_flags_t item_flags;
		sys_monitor_events_t item_events;

		if (context_is_item) {
			sys_mutex_lock(&item->mutex);
			item_flags = item->flags;
			item_events = item->events;
			monitored_and_triggered = libsyscall_monitor_events_to_sys_monitor_events(event->events) & item->events;
			sys_mutex_unlock(&item->mutex);

			// FIXME: we need the kernel to tell us when it has disabled an item indirectly (i.e. via disable-on-trigger)

			if (sys_object_class(item->target) == sys_object_class_counter()) {
				if (monitored_and_triggered & libsyscall_monitor_event_futex_awoken) {
					if ((item_flags & sys_monitor_item_flag_disable_on_trigger) == 0 && (item_flags & sys_monitor_item_flag_enabled) != 0) {
						// the user doesn't want to disable the item on trigger,
						// but we always have to do so (required by the kernel),
						// so let's re-enable the item
						ferr_t update_status = ferr_ok;
						libsyscall_monitor_update_item_t update_item;
						uint64_t update_item_count = 1;

						update_item.header.id = event->header.id;
						update_item.header.descriptor_id = event->header.descriptor_id;
						update_item.header.type = event->header.type;
						update_item.header.context = (uintptr_t)item;

						// we always request the "item deleted" event so we know when it's safe to release an item
						update_item.events = sys_monitor_events_to_libsyscall_monitor_events(item_events) | libsyscall_monitor_event_item_deleted;
						update_item.flags = libsyscall_monitor_update_item_flag_update | libsyscall_monitor_update_item_flag_strict_match | sys_monitor_item_flags_to_libsyscall_monitor_update_item_flags(item_flags) | libsyscall_monitor_update_item_flag_disable_on_trigger;
						update_item.status = ferr_ok;
						update_item.data1 = __atomic_or_fetch(&((sys_counter_object_t*)item->target)->value, sys_counter_flag_need_to_wake, __ATOMIC_RELAXED);

						sys_mutex_lock(&monitor->mutex);
						update_status = libsyscall_wrapper_monitor_update(monitor->monitor_did, 0, &update_item, &update_item_count);
						sys_mutex_unlock(&monitor->mutex);

						if (update_status == ferr_ok) {
							update_status = update_item.status;
						}

						if (update_item.status == ferr_no_such_resource) {
							// someone else may have deleted the item from the monitor
							// before we were able to re-enable it. just ignore this error then.
						} else {
							// in all other cases, this should succeed
							sys_abort_status(update_item.status);
						}
					}
				}
			}

			if (monitored_and_triggered != 0) {
				// we actually care about this event

				// retain the object (the caller always receives a reference to the item)
				// it's impossible for this to fail since the monitor should still be holding a reference to the item at this point
				// (even if the item is dead)
				sys_abort_status(sys_retain(item_obj));

				poll_item->events = monitored_and_triggered;
				poll_item->item = item_obj;
				poll_item->type = sys_monitor_poll_item_type_item;

				++out_item_count;
			}

			if (event->events & libsyscall_monitor_event_item_deleted) {
				// even if the user doesn't care about this event, we do.
				// we always request this event so we know when items are deleted in the kernel
				// and thus are safe to delete in userspace.

				sys_mutex_lock(&monitor->mutex);
				for (size_t j = monitor->item_count; j < monitor->array_size; ++j) {
					if (monitor->items[j] == item_obj) {
						simple_memmove(&monitor->items[j], &monitor->items[j + 1], ((monitor->array_size - j) - 1) * sizeof(*monitor->items));
						--monitor->array_size;
						// try to shrink the array, but ignore failure
						LIBSYS_WUR_IGNORE(sys_mempool_reallocate(monitor->items, monitor->array_size * sizeof(*monitor->items), NULL, (void*)&monitor->items));
						break;
					}
				}
				sys_mutex_unlock(&monitor->mutex);

				// release the item
				sys_release(item_obj);
			}
		} else {
			// this is a oneshot item
			if (event->header.type == libsyscall_monitor_item_type_futex) {
				poll_item->futex_context = (void*)event->header.context;
				poll_item->type = sys_monitor_poll_item_type_futex;
			} else if (event->header.type == libsyscall_monitor_item_type_timeout) {
				poll_item->timeout_context = (void*)event->header.context;
				poll_item->type = sys_monitor_poll_item_type_timeout;
			}
			++out_item_count;
		}
	}

	sys_mutex_lock(&monitor->mutex);
	--monitor->outstanding_polls;
	sys_mutex_unlock(&monitor->mutex);

out:
	if (obj) {
		sys_release(obj);
	}
	*in_out_item_count = out_item_count;
	return status;
};

uint64_t sys_monitor_item_descriptor_id(sys_monitor_item_object_t* item) {
	const sys_object_class_t* obj_class = sys_object_class(item->target);

	if (obj_class == sys_object_class_channel()) {
		return ((sys_channel_object_t*)item->target)->channel_did;
	} else if (obj_class == sys_object_class_counter()) {
		return (uintptr_t)&((sys_counter_object_t*)item->target)->value;
	} else {
		// maybe abort?
		return UINT64_MAX;
	}
};

libsyscall_monitor_item_type_t sys_monitor_item_type(sys_monitor_item_object_t* item) {
	const sys_object_class_t* obj_class = sys_object_class(item->target);

	if (obj_class == sys_object_class_channel()) {
		return libsyscall_monitor_item_type_channel;
	} else if (obj_class == sys_object_class_counter()) {
		return libsyscall_monitor_item_type_futex;
	} else {
		// maybe abort?
		return libsyscall_monitor_item_type_invalid;
	}
};

ferr_t sys_monitor_oneshot_futex(sys_monitor_t* obj, uint64_t* address, uint64_t channel, uint64_t expected_value, void* context) {
	sys_monitor_object_t* monitor = (void*)obj;
	libsyscall_monitor_update_item_t item;
	uint64_t count = 1;
	ferr_t status = ferr_ok;

	item.header.id = libsyscall_monitor_item_id_none;
	item.header.type = libsyscall_monitor_item_type_futex;
	item.header.descriptor_id = (uintptr_t)address;
	item.header.context = (uintptr_t)context;
	item.events = libsyscall_monitor_event_futex_awoken;
	item.flags = libsyscall_monitor_update_item_flag_create | libsyscall_monitor_update_item_flag_enabled | libsyscall_monitor_update_item_flag_edge_triggered | libsyscall_monitor_update_item_flag_active_high | libsyscall_monitor_update_item_flag_delete_on_trigger | libsyscall_monitor_update_item_flag_set_user_flag;
	item.data1 = channel;
	item.data2 = expected_value;
	item.status = ferr_ok;

	status = libsyscall_wrapper_monitor_update(monitor->monitor_did, 0, &item, &count);
	if (status == ferr_ok) {
		status = item.status;
	}

	return status;
};

ferr_t sys_monitor_oneshot_timeout(sys_monitor_t* obj, uint64_t timeout, sys_timeout_type_t timeout_type, void* context) {
	sys_monitor_object_t* monitor = (void*)obj;
	libsyscall_monitor_update_item_t item;
	uint64_t count = 1;
	ferr_t status = ferr_ok;

	item.header.id = libsyscall_monitor_item_id_none;
	item.header.type = libsyscall_monitor_item_type_timeout;
	item.header.descriptor_id = (uintptr_t)timeout;
	item.header.context = (uintptr_t)context;
	item.events = libsyscall_monitor_event_timeout_expired;
	item.flags = libsyscall_monitor_update_item_flag_create | libsyscall_monitor_update_item_flag_enabled | libsyscall_monitor_update_item_flag_edge_triggered | libsyscall_monitor_update_item_flag_active_high | libsyscall_monitor_update_item_flag_delete_on_trigger | libsyscall_monitor_update_item_flag_set_user_flag;
	item.data1 = sys_timeout_type_to_libsyscall_timeout_type(timeout_type);
	item.data2 = 0;
	item.status = ferr_ok;

	status = libsyscall_wrapper_monitor_update(monitor->monitor_did, 0, &item, &count);
	if (status == ferr_ok) {
		status = item.status;
	}

	return status;
};
