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

#include <gen/ferro/userspace/syscall-handlers.h>
#include <ferro/userspace/processes.h>
#include <ferro/syscalls/monitors.private.h>
#include <ferro/core/mempool.h>
#include <libsimple/libsimple.h>
#include <ferro/core/panic.h>
#include <ferro/syscalls/channels.private.h>
#include <ferro/core/waitq.private.h>

// TODO: modularize this and allow monitor items to be managed in separate sources

// TODO: optimize waiters so that we only wait for events the user is interested in

static ferr_t fsyscall_monitor_item_retain(fsyscall_monitor_item_t* item);
static void fsyscall_monitor_item_release(fsyscall_monitor_item_t* item);

static ferr_t fsyscall_monitor_retain(fsyscall_monitor_t* monitor);
static void fsyscall_monitor_release(fsyscall_monitor_t* monitor);

// FIXME: these waiters are (very briefly) potentially racing with the destruction of the monitor item

FERRO_STRUCT(fsyscall_monitor_item_channel_common_info) {
	union {
		fsyscall_monitor_item_t* item;
		fsyscall_monitor_item_channel_t* channel_item;
		fsyscall_monitor_item_server_channel_t* server_channel_item;
	};
	fsyscall_monitor_t* monitor;
	bool edge_triggered;
	bool active_low;
};

static bool fsyscall_monitor_item_channel_common_start(void* context, fsyscall_monitor_item_channel_common_info_t* out_info) {
	out_info->item = context;

	if (fsyscall_monitor_item_retain(out_info->item) != ferr_ok) {
		out_info->item = NULL;
		return false;
	}

	out_info->monitor = out_info->item->monitor;

	if (!out_info->monitor || fsyscall_monitor_retain(out_info->monitor) != ferr_ok) {
		out_info->monitor = NULL;
		fsyscall_monitor_item_release(out_info->item);
		out_info->item = NULL;
		return false;
	}

	flock_mutex_lock(&out_info->monitor->mutex);

	out_info->edge_triggered = (out_info->item->flags & fsyscall_monitor_item_flag_edge_triggered) != 0;
	out_info->active_low = (out_info->item->flags & fsyscall_monitor_item_flag_active_low) != 0;

	return true;
};

static void fsyscall_monitor_item_channel_common_end(fsyscall_monitor_item_channel_common_info_t* in_info, bool triggered) {
	flock_mutex_unlock(&in_info->monitor->mutex);

	if (triggered) {
		flock_semaphore_up(&in_info->monitor->triggered_items_semaphore);
	}

	if (in_info->monitor) {
		fsyscall_monitor_release(in_info->monitor);
	}

	if (in_info->item) {
		fsyscall_monitor_item_release(in_info->item);
	}
};

static bool fsyscall_monitor_item_channel_process_trigger(fsyscall_monitor_item_channel_common_info_t* info, fsyscall_monitor_events_t event, bool prev_high, bool curr_high) {
	if (info->edge_triggered) {
		if (curr_high != prev_high && !info->active_low == curr_high) {
			info->item->triggered_events |= event;
			return true;
		}
	} else {
		if (!info->active_low == curr_high) {
			info->item->triggered_events |= event;
			return true;
		}
	}

	return false;
};

static void fsyscall_monitor_item_channel_message_arrival(void* context) {
	fsyscall_monitor_item_channel_common_info_t info;
	bool triggered = false;
	bool prev_message_arrival_high;
	bool prev_queue_empty_high;

	if (!fsyscall_monitor_item_channel_common_start(context, &info)) {
		return;
	}

	prev_message_arrival_high = info.channel_item->message_arrival_high;
	prev_queue_empty_high = info.channel_item->queue_empty_high;

	info.channel_item->message_arrival_high = true;
	info.channel_item->queue_empty_high = false;

	triggered |= fsyscall_monitor_item_channel_process_trigger(&info, fsyscall_monitor_event_channel_message_arrived, prev_message_arrival_high, info.channel_item->message_arrival_high);
	triggered |= fsyscall_monitor_item_channel_process_trigger(&info, fsyscall_monitor_event_channel_queue_emptied, prev_queue_empty_high, info.channel_item->queue_empty_high);

	flock_mutex_lock(&info.monitor->mutex);
	if (info.channel_item->base.flags & fsyscall_monitor_item_flag_enabled) {
		fwaitq_wait(&info.channel_item->channel->message_arrival_waitq, &info.channel_item->message_arrival_waiter);
	}
	flock_mutex_unlock(&info.monitor->mutex);

	fsyscall_monitor_item_channel_common_end(&info, triggered);
};

static void fsyscall_monitor_item_channel_queue_empty(void* context) {
	fsyscall_monitor_item_channel_common_info_t info;
	bool triggered = false;
	bool prev_message_arrival_high;
	bool prev_queue_empty_high;

	if (!fsyscall_monitor_item_channel_common_start(context, &info)) {
		return;
	}

	prev_message_arrival_high = info.channel_item->message_arrival_high;
	prev_queue_empty_high = info.channel_item->queue_empty_high;

	info.channel_item->message_arrival_high = false;
	info.channel_item->queue_empty_high = true;

	triggered |= fsyscall_monitor_item_channel_process_trigger(&info, fsyscall_monitor_event_channel_message_arrived, prev_message_arrival_high, info.channel_item->message_arrival_high);
	triggered |= fsyscall_monitor_item_channel_process_trigger(&info, fsyscall_monitor_event_channel_queue_emptied, prev_queue_empty_high, info.channel_item->queue_empty_high);

	flock_mutex_lock(&info.monitor->mutex);
	if (info.channel_item->base.flags & fsyscall_monitor_item_flag_enabled) {
		fwaitq_wait(&info.channel_item->channel->queue_empty_waitq, &info.channel_item->queue_empty_waiter);
	}
	flock_mutex_unlock(&info.monitor->mutex);

	fsyscall_monitor_item_channel_common_end(&info, triggered);
};

static void fsyscall_monitor_item_channel_peer_queue_empty(void* context) {
	fsyscall_monitor_item_channel_common_info_t info;
	bool triggered = false;
	bool prev_peer_queue_empty_high;

	if (!fsyscall_monitor_item_channel_common_start(context, &info)) {
		return;
	}

	prev_peer_queue_empty_high = info.channel_item->peer_queue_empty_high;

	info.channel_item->peer_queue_empty_high = true;

	triggered |= fsyscall_monitor_item_channel_process_trigger(&info, fsyscall_monitor_event_channel_peer_queue_emptied, prev_peer_queue_empty_high, info.channel_item->peer_queue_empty_high);

	flock_mutex_lock(&info.monitor->mutex);
	if (info.channel_item->base.flags & fsyscall_monitor_item_flag_enabled) {
		fwaitq_wait(&fchannel_peer(info.channel_item->channel, false)->queue_empty_waitq, &info.channel_item->peer_queue_empty_waiter);
	}
	flock_mutex_unlock(&info.monitor->mutex);

	fsyscall_monitor_item_channel_common_end(&info, triggered);
};

static void fsyscall_monitor_item_channel_peer_message_arrival(void* context) {
	fsyscall_monitor_item_channel_common_info_t info;
	bool triggered = false;
	bool prev_peer_queue_empty_high;

	if (!fsyscall_monitor_item_channel_common_start(context, &info)) {
		return;
	}

	prev_peer_queue_empty_high = info.channel_item->peer_queue_empty_high;

	info.channel_item->peer_queue_empty_high = false;

	triggered |= fsyscall_monitor_item_channel_process_trigger(&info, fsyscall_monitor_event_channel_peer_queue_emptied, prev_peer_queue_empty_high, info.channel_item->peer_queue_empty_high);

	flock_mutex_lock(&info.monitor->mutex);
	if (info.channel_item->base.flags & fsyscall_monitor_item_flag_enabled) {
		fwaitq_wait(&fchannel_peer(info.channel_item->channel, false)->message_arrival_waitq, &info.channel_item->peer_message_arrival_waiter);
	}
	flock_mutex_unlock(&info.monitor->mutex);

	fsyscall_monitor_item_channel_common_end(&info, triggered);
};

static void fsyscall_monitor_item_channel_peer_close(void* context) {
	fsyscall_monitor_item_channel_common_info_t info;
	bool triggered = false;
	bool prev_peer_close;

	if (!fsyscall_monitor_item_channel_common_start(context, &info)) {
		return;
	}

	prev_peer_close = info.channel_item->peer_close_high;

	info.channel_item->peer_close_high = true;

	triggered |= fsyscall_monitor_item_channel_process_trigger(&info, fsyscall_monitor_event_channel_peer_closed, prev_peer_close, info.channel_item->peer_close_high);

	// channels can't close twice
	//fwaitq_wait(&fchannel_peer(info.channel_item->channel, false)->close_waitq, &info.channel_item->peer_close_waiter);

	fsyscall_monitor_item_channel_common_end(&info, triggered);
};

static void fsyscall_monitor_item_channel_peer_queue_removal(void* context) {
	fsyscall_monitor_item_channel_common_info_t info;
	bool triggered = false;
	bool prev_peer_queue_space_available;

	if (!fsyscall_monitor_item_channel_common_start(context, &info)) {
		return;
	}

	prev_peer_queue_space_available = info.channel_item->peer_queue_space_available_high;

	info.channel_item->peer_queue_space_available_high = true;

	triggered |= fsyscall_monitor_item_channel_process_trigger(&info, fsyscall_monitor_event_channel_peer_queue_space_available, prev_peer_queue_space_available, info.channel_item->peer_queue_space_available_high);

	flock_mutex_lock(&info.monitor->mutex);
	if (info.channel_item->base.flags & fsyscall_monitor_item_flag_enabled) {
		fwaitq_wait(&fchannel_peer(info.channel_item->channel, false)->queue_removal_waitq, &info.channel_item->peer_queue_removal_waiter);
	}
	flock_mutex_unlock(&info.monitor->mutex);

	fsyscall_monitor_item_channel_common_end(&info, triggered);
};

static void fsyscall_monitor_item_channel_peer_queue_full(void* context) {
	fsyscall_monitor_item_channel_common_info_t info;
	bool triggered = false;
	bool prev_peer_queue_space_available;

	if (!fsyscall_monitor_item_channel_common_start(context, &info)) {
		return;
	}

	prev_peer_queue_space_available = info.channel_item->peer_queue_space_available_high;

	info.channel_item->peer_queue_space_available_high = false;

	triggered |= fsyscall_monitor_item_channel_process_trigger(&info, fsyscall_monitor_event_channel_peer_queue_space_available, prev_peer_queue_space_available, info.channel_item->peer_queue_space_available_high);

	flock_mutex_lock(&info.monitor->mutex);
	if (info.channel_item->base.flags & fsyscall_monitor_item_flag_enabled) {
		fwaitq_wait(&fchannel_peer(info.channel_item->channel, false)->queue_full_waitq, &info.channel_item->peer_queue_full_waiter);
	}
	flock_mutex_unlock(&info.monitor->mutex);

	fsyscall_monitor_item_channel_common_end(&info, triggered);
};

static void fsyscall_monitor_item_channel_close(void* context) {
	fsyscall_monitor_item_channel_common_info_t info;
	bool triggered = false;
	bool prev_close;

	if (!fsyscall_monitor_item_channel_common_start(context, &info)) {
		return;
	}

	prev_close = info.channel_item->close_high;

	info.channel_item->close_high = true;

	// TODO: actually use this information and delete monitor items if the "keep alive" flag is unset

	fsyscall_monitor_item_channel_common_end(&info, triggered);
};

static void fsyscall_monitor_item_server_channel_client_arrival(void* context) {
	fsyscall_monitor_item_channel_common_info_t info;
	bool triggered = false;
	bool prev_client_arrival;

	if (!fsyscall_monitor_item_channel_common_start(context, &info)) {
		return;
	}

	prev_client_arrival = info.server_channel_item->client_arrival_high;

	info.server_channel_item->client_arrival_high = true;

	triggered |= fsyscall_monitor_item_channel_process_trigger(&info, fsyscall_monitor_event_server_channel_client_arrived, prev_client_arrival, info.server_channel_item->client_arrival_high);

	flock_mutex_lock(&info.monitor->mutex);
	if (info.channel_item->base.flags & fsyscall_monitor_item_flag_enabled) {
		fwaitq_wait(&info.server_channel_item->server_channel->client_arrival_waitq, &info.server_channel_item->client_arrival_waiter);
	}
	flock_mutex_unlock(&info.monitor->mutex);

	fsyscall_monitor_item_channel_common_end(&info, triggered);
};

static void fsyscall_monitor_item_server_channel_queue_empty(void* context) {
	fsyscall_monitor_item_channel_common_info_t info;
	bool triggered = false;
	bool prev_client_arrival;

	if (!fsyscall_monitor_item_channel_common_start(context, &info)) {
		return;
	}

	prev_client_arrival = info.server_channel_item->client_arrival_high;

	info.server_channel_item->client_arrival_high = false;

	triggered |= fsyscall_monitor_item_channel_process_trigger(&info, fsyscall_monitor_event_server_channel_client_arrived, prev_client_arrival, info.server_channel_item->client_arrival_high);

	flock_mutex_lock(&info.monitor->mutex);
	if (info.channel_item->base.flags & fsyscall_monitor_item_flag_enabled) {
		fwaitq_wait(&info.server_channel_item->server_channel->queue_empty_waitq, &info.server_channel_item->queue_empty_waiter);
	}
	flock_mutex_unlock(&info.monitor->mutex);

	fsyscall_monitor_item_channel_common_end(&info, triggered);
};

static void fsyscall_monitor_item_server_channel_close(void* context) {
	fsyscall_monitor_item_channel_common_info_t info;
	bool triggered = false;
	bool prev_close;

	if (!fsyscall_monitor_item_channel_common_start(context, &info)) {
		return;
	}

	prev_close = info.server_channel_item->close_high;

	info.server_channel_item->close_high = true;

	// TODO: actually use this information and delete monitor items if the "keep alive" flag is unset

	fsyscall_monitor_item_channel_common_end(&info, triggered);
};

static void fsyscall_monitor_item_futex_wakeup(void* context) {
	fsyscall_monitor_item_futex_t* futex_item = context;
	fsyscall_monitor_t* monitor;

	if (fsyscall_monitor_item_retain(&futex_item->base) != ferr_ok) {
		return;
	}

	monitor = futex_item->base.monitor;

	if (!monitor || fsyscall_monitor_retain(monitor) != ferr_ok) {
		fsyscall_monitor_item_release(&futex_item->base);
		return;
	}

	flock_mutex_lock(&monitor->mutex);

	futex_item->base.triggered_events |= fsyscall_monitor_event_futex_awoken;

	if (futex_item->base.flags & fsyscall_monitor_item_flag_enabled) {
		fwaitq_wait(&futex_item->futex->waitq, &futex_item->waiter);
	}

	flock_mutex_unlock(&monitor->mutex);

	flock_semaphore_up(&monitor->triggered_items_semaphore);

	fsyscall_monitor_release(monitor);
	fsyscall_monitor_item_release(&futex_item->base);
};

static void fsyscall_monitor_item_timeout_expire(void* context) {
	fsyscall_monitor_item_timeout_t* timeout_item = context;
	fsyscall_monitor_t* monitor;

	if (fsyscall_monitor_item_retain(&timeout_item->base) != ferr_ok) {
		return;
	}

	monitor = timeout_item->base.monitor;

	if (!monitor || fsyscall_monitor_retain(monitor) != ferr_ok) {
		fsyscall_monitor_item_release(&timeout_item->base);
		return;
	}

	flock_mutex_lock(&monitor->mutex);

	timeout_item->base.triggered_events |= fsyscall_monitor_event_futex_awoken;

	if (timeout_item->base.flags & fsyscall_monitor_item_flag_enabled) {
		if (timeout_item->work) {
			fwork_release(timeout_item->work);
		}
		timeout_item->work = NULL;
		FERRO_WUR_IGNORE(fwork_schedule_new(fsyscall_monitor_item_timeout_expire, timeout_item, timeout_item->base.header.descriptor_id, &timeout_item->work));
	}

	flock_mutex_unlock(&monitor->mutex);

	flock_semaphore_up(&monitor->triggered_items_semaphore);

	fsyscall_monitor_release(monitor);
	fsyscall_monitor_item_release(&timeout_item->base);
};

static ferr_t fsyscall_monitor_item_disable(fsyscall_monitor_item_t* item) {
	item->flags &= ~fsyscall_monitor_item_flag_enabled;

	switch (item->header.type) {
		case fsyscall_monitor_item_type_channel: {
			fsyscall_monitor_item_channel_t* channel_item = (void*)item;
			fchannel_t* channel = channel_item->channel;
			fchannel_t* peer = fchannel_peer(channel_item->channel, false);

			fwaitq_unwait(&channel->message_arrival_waitq, &channel_item->message_arrival_waiter);
			fwaitq_unwait(&channel->queue_empty_waitq, &channel_item->queue_empty_waiter);
			fwaitq_unwait(&peer->queue_empty_waitq, &channel_item->peer_queue_empty_waiter);
			fwaitq_unwait(&peer->message_arrival_waitq, &channel_item->peer_message_arrival_waiter);
			fwaitq_unwait(&peer->close_waitq, &channel_item->peer_close_waiter);
			fwaitq_unwait(&peer->queue_removal_waitq, &channel_item->peer_queue_removal_waiter);
			fwaitq_unwait(&peer->queue_full_waitq, &channel_item->peer_queue_full_waiter);
			fwaitq_unwait(&channel->close_waitq, &channel_item->close_waiter);
		} break;

		case fsyscall_monitor_item_type_server_channel: {
			fsyscall_monitor_item_server_channel_t* server_channel_item = (void*)item;
			fchannel_server_t* server_channel = server_channel_item->server_channel;

			fwaitq_unwait(&server_channel->client_arrival_waitq, &server_channel_item->client_arrival_waiter);
			fwaitq_unwait(&server_channel->queue_empty_waitq, &server_channel_item->queue_empty_waiter);
			fwaitq_unwait(&server_channel->close_waitq, &server_channel_item->close_waiter);
		} break;

		case fsyscall_monitor_item_type_futex: {
			fsyscall_monitor_item_futex_t* futex_item = (void*)item;

			fwaitq_unwait(&futex_item->futex->waitq, &futex_item->waiter);
		} break;

		case fsyscall_monitor_item_type_timeout: {
			fsyscall_monitor_item_timeout_t* timeout_item = (void*)item;

			if (timeout_item->work) {
				// FIXME: handle the case where the work is already running.
				//        this is currently just a race condition.
				FERRO_WUR_IGNORE(fwork_cancel(timeout_item->work));
				fwork_release(timeout_item->work);
			}
			timeout_item->work = NULL;
		} break;
	}

	return ferr_ok;
};

static ferr_t fsyscall_monitor_item_enable(fsyscall_monitor_item_t* item) {
	item->flags |= fsyscall_monitor_item_flag_enabled;

	switch (item->header.type) {
		case fsyscall_monitor_item_type_channel: {
			fsyscall_monitor_item_channel_t* channel_item = (void*)item;
			fchannel_t* peer = fchannel_peer(channel_item->channel, false);

			fwaitq_waiter_init(&channel_item->message_arrival_waiter, fsyscall_monitor_item_channel_message_arrival, channel_item);
			fwaitq_waiter_init(&channel_item->queue_empty_waiter, fsyscall_monitor_item_channel_queue_empty, channel_item);
			fwaitq_waiter_init(&channel_item->peer_queue_empty_waiter, fsyscall_monitor_item_channel_peer_queue_empty, channel_item);
			fwaitq_waiter_init(&channel_item->peer_message_arrival_waiter, fsyscall_monitor_item_channel_peer_message_arrival, channel_item);
			fwaitq_waiter_init(&channel_item->peer_close_waiter, fsyscall_monitor_item_channel_peer_close, channel_item);
			fwaitq_waiter_init(&channel_item->peer_queue_removal_waiter, fsyscall_monitor_item_channel_peer_queue_removal, channel_item);
			fwaitq_waiter_init(&channel_item->peer_queue_full_waiter, fsyscall_monitor_item_channel_peer_queue_full, channel_item);
			fwaitq_waiter_init(&channel_item->close_waiter, fsyscall_monitor_item_channel_close, channel_item);

			fwaitq_wait(&channel_item->channel->message_arrival_waitq, &channel_item->message_arrival_waiter);
			fwaitq_wait(&channel_item->channel->queue_empty_waitq, &channel_item->queue_empty_waiter);
			fwaitq_wait(&peer->queue_empty_waitq, &channel_item->peer_queue_empty_waiter);
			fwaitq_wait(&peer->message_arrival_waitq, &channel_item->peer_message_arrival_waiter);
			fwaitq_wait(&peer->close_waitq, &channel_item->peer_close_waiter);
			fwaitq_wait(&peer->queue_removal_waitq, &channel_item->peer_queue_removal_waiter);
			fwaitq_wait(&peer->queue_full_waitq, &channel_item->peer_queue_full_waiter);
			fwaitq_wait(&channel_item->channel->close_waitq, &channel_item->close_waiter);
		} break;

		case fsyscall_monitor_item_type_server_channel: {
			fsyscall_monitor_item_server_channel_t* server_channel_item = (void*)item;

			fwaitq_waiter_init(&server_channel_item->client_arrival_waiter, fsyscall_monitor_item_server_channel_client_arrival, server_channel_item);
			fwaitq_waiter_init(&server_channel_item->queue_empty_waiter, fsyscall_monitor_item_server_channel_queue_empty, server_channel_item);
			fwaitq_waiter_init(&server_channel_item->close_waiter, fsyscall_monitor_item_server_channel_close, server_channel_item);

			fwaitq_wait(&server_channel_item->server_channel->client_arrival_waitq, &server_channel_item->client_arrival_waiter);
			fwaitq_wait(&server_channel_item->server_channel->queue_empty_waitq, &server_channel_item->queue_empty_waiter);
			fwaitq_wait(&server_channel_item->server_channel->close_waitq, &server_channel_item->close_waiter);
		} break;

		case fsyscall_monitor_item_type_futex: {
			fsyscall_monitor_item_futex_t* futex_item = (void*)item;
			uint64_t curr_val;

			fwaitq_waiter_init(&futex_item->waiter, fsyscall_monitor_item_futex_wakeup, futex_item);

			// check if the value currently in the futex address is what we expected.
			// if it doesn't match up, we immediately trigger the item (so the user knows
			// to recheck the futex).

			// see futex_wait.c for why we check the value and add ourselves while holding the waitq lock
			fwaitq_lock(&futex_item->futex->waitq);
			curr_val = __atomic_load_n((uint64_t*)futex_item->futex->address, __ATOMIC_RELAXED);
			fwaitq_add_locked(&futex_item->futex->waitq, &futex_item->waiter);
			fwaitq_unlock(&futex_item->futex->waitq);

			if (curr_val != futex_item->expected_value) {
				fsyscall_monitor_item_futex_wakeup(futex_item);
			}
		} break;

		case fsyscall_monitor_item_type_timeout: {
			fsyscall_monitor_item_timeout_t* timeout_item = (void*)item;

			if (timeout_item->work) {
				// FIXME: handle the case where the work is already running.
				//        this is currently just a race condition.
				FERRO_WUR_IGNORE(fwork_cancel(timeout_item->work));
				fwork_release(timeout_item->work);
			}
			timeout_item->work = NULL;
			FERRO_WUR_IGNORE(fwork_schedule_new(fsyscall_monitor_item_timeout_expire, timeout_item, timeout_item->base.header.descriptor_id, &timeout_item->work));
		} break;
	}

	return ferr_ok;
};

static ferr_t fsyscall_monitor_item_create(const fsyscall_monitor_item_header_t* header, fsyscall_monitor_events_t events, fsyscall_monitor_item_flags_t flags, fsyscall_monitor_t* monitor, uint64_t data1, uint64_t data2, fsyscall_monitor_item_t** out_item) {
	ferr_t status = ferr_ok;
	fsyscall_monitor_item_t* item = NULL;
	size_t size = 0;
	bool release_monitor_on_fail = false;
	void* descriptor = NULL;
	const fproc_descriptor_class_t* desc_class = NULL;
	const fproc_descriptor_class_t* expected_desc_class = NULL;
	futex_t* futex = NULL;

	switch (header->type) {
		case fsyscall_monitor_item_type_channel:
			size = sizeof(fsyscall_monitor_item_channel_t);
			expected_desc_class = &fsyscall_channel_descriptor_class;
			break;
		case fsyscall_monitor_item_type_server_channel:
			size = sizeof(fsyscall_monitor_item_server_channel_t);
			expected_desc_class = &fsyscall_channel_server_context_descriptor_class;
			break;

		case fsyscall_monitor_item_type_futex: {
			size = sizeof(fsyscall_monitor_item_futex_t);
			// descriptor_id is actually an address, so no expected_desc_class.
			// instead, let's look up the futex
			status = futex_lookup(&fproc_current()->futex_table, header->descriptor_id, data1, &futex);
			if (status != ferr_ok) {
				goto out;
			}
		} break;

		case fsyscall_monitor_item_type_timeout: {
			size = sizeof(fsyscall_monitor_item_timeout_t);
			if (data1 != fsyscall_timeout_type_ns_relative) {
				// TODO: support other timeout types
				status = ferr_invalid_argument;
				goto out;
			}
		} break;

		default:
			status = ferr_invalid_argument;
			goto out;
	}

	if (expected_desc_class) {
		status = fproc_lookup_descriptor(fproc_current(), header->descriptor_id, true, &descriptor, &desc_class);
		if (status != ferr_ok) {
			goto out;
		}

		if (desc_class != expected_desc_class) {
			status = ferr_invalid_argument;
			goto out;
		}
	}

	status = fmempool_allocate(size, NULL, (void*)&item);
	if (status != ferr_ok) {
		goto out;
	}

	simple_memset(item, 0, size);

	frefcount_init(&item->refcount);

	// waiters hold a reference on the item to prevent it from being released while they're not looking
	FERRO_WUR_IGNORE(frefcount_increment(&item->refcount));

	simple_memcpy(&item->header, header, sizeof(item->header));
	item->flags = flags;
	item->monitored_events = events;
	item->monitor = monitor;

	while (true) {
		item->header.id = __atomic_fetch_add(&monitor->next_item_id, 1, __ATOMIC_RELAXED);
		if (item->header.id != fsyscall_monitor_item_id_none) {
			break;
		}
	}

	// this can't fail since the monitor reference must be valid here
	fpanic_status(fsyscall_monitor_retain(monitor));
	release_monitor_on_fail = true;

	// now initialize item-specific members

	switch (header->type) {
		case fsyscall_monitor_item_type_channel: {
			fsyscall_monitor_item_channel_t* channel_item = (void*)item;
			channel_item->channel = descriptor;
		} break;

		case fsyscall_monitor_item_type_server_channel: {
			fsyscall_monitor_item_server_channel_t* server_channel_item = (void*)item;
			server_channel_item->server_channel = ((fsyscall_channel_server_context_t*)descriptor)->server;
		} break;

		case fsyscall_monitor_item_type_futex: {
			fsyscall_monitor_item_futex_t* futex_item = (void*)item;
			futex_item->futex = futex;
			futex_item->expected_value = data2;
		} break;

		case fsyscall_monitor_item_type_timeout: {
			fsyscall_monitor_item_timeout_t* timeout_item = (void*)item;
			timeout_item->work = NULL;
		} break;

		default:
			fpanic("impossible: header type changed");
	}

	if (item->flags & fsyscall_monitor_item_flag_enabled) {
		fsyscall_monitor_item_enable(item);
	}

out:
	if (status == ferr_ok) {
		*out_item = item;
	} else {
		if (release_monitor_on_fail) {
			fsyscall_monitor_release(monitor);
		}

		if (item) {
			FERRO_WUR_IGNORE(fmempool_free(item));
		}

		if (desc_class) {
			desc_class->release(descriptor);
		}

		if (futex) {
			futex_release(futex);
		}
	}
	return status;
};

static void fsyscall_monitor_item_delete(fsyscall_monitor_item_t* item) {
	fsyscall_monitor_t* monitor = item->monitor;

	item->monitor = NULL;

	if (item->flags & fsyscall_monitor_item_flag_enabled) {
		fsyscall_monitor_item_disable(item);
	}

	switch (item->header.type) {
		case fsyscall_monitor_item_type_channel: {
			fsyscall_monitor_item_channel_t* channel_item = (void*)item;
			fchannel_t* channel = channel_item->channel;
			channel_item->channel = NULL;
			fchannel_release(channel);
		} break;

		case fsyscall_monitor_item_type_server_channel: {
			fsyscall_monitor_item_server_channel_t* server_channel_item = (void*)item;
			fchannel_server_t* server_channel = server_channel_item->server_channel;
			server_channel_item->server_channel = NULL;
			fchannel_server_release(server_channel);
		} break;

		case fsyscall_monitor_item_type_futex: {
			fsyscall_monitor_item_futex_t* futex_item = (void*)item;
			futex_t* futex = futex_item->futex;
			futex_item->futex = NULL;
			futex_release(futex);
		} break;

		case fsyscall_monitor_item_type_timeout: {
			// nothing
		} break;
	}

	fsyscall_monitor_release(monitor);

	// release the reference that waiters hold on the item
	fsyscall_monitor_item_release(item);
};

static ferr_t fsyscall_monitor_item_retain(fsyscall_monitor_item_t* item) {
	return frefcount_increment(&item->refcount);
};

static void fsyscall_monitor_item_release(fsyscall_monitor_item_t* item) {
	if (frefcount_decrement(&item->refcount) != ferr_permanent_outage) {
		return;
	}

	FERRO_WUR_IGNORE(fmempool_free(item));
};

static ferr_t fsyscall_monitor_item_poll(fsyscall_monitor_item_t* item, fsyscall_monitor_event_t* event) {
	ferr_t status = ferr_ok;
	fsyscall_monitor_events_t triggered_events = item->triggered_events;

	if ((item->flags & (fsyscall_monitor_item_flag_enabled | fsyscall_monitor_item_flag_dead)) == 0 || (item->triggered_events & item->monitored_events) == 0) {
		status = ferr_temporary_outage;
		goto out;
	}

	event->events = (item->triggered_events & item->monitored_events);
	event->flags = 0;
	simple_memcpy(&event->header, &item->header, sizeof(event->header));

	if ((item->flags & fsyscall_monitor_item_flag_set_user_flag) != 0) {
		event->flags |= fsyscall_monitor_event_flag_user;
	}

	if ((item->flags & fsyscall_monitor_item_flag_edge_triggered) != 0) {
		// if it's edge-triggered, we're responsible for clearing the triggered events;
		// if it's level-triggered, the event sources are responsible for clearing the triggered events.
		item->triggered_events = 0;
	} else if ((item->flags & fsyscall_monitor_item_flag_dead) == 0) {
		// if it's level-triggered, we need to re-increment the semaphore because it'll keep triggering constantly
		flock_semaphore_up(&item->monitor->triggered_items_semaphore);
	}

out:
	return status;
};

static ferr_t fsyscall_monitor_retain(fsyscall_monitor_t* monitor) {
	return frefcount_increment(&monitor->refcount);
};

static void fsyscall_monitor_release(fsyscall_monitor_t* monitor) {
	if (frefcount_decrement(&monitor->refcount) != ferr_permanent_outage) {
		return;
	}

	FERRO_WUR_IGNORE(fmempool_free(monitor));
};

static const fproc_descriptor_class_t fsyscall_monitor_descriptor_class = {
	.retain = (void*)fsyscall_monitor_retain,
	.release = (void*)fsyscall_monitor_release,
};

ferr_t fsyscall_handler_monitor_create(uint64_t* out_monitor_handle) {
	ferr_t status = ferr_ok;
	fsyscall_monitor_t* monitor = NULL;
	uint64_t monitor_handle = FPROC_DID_MAX;

	status = fmempool_allocate(sizeof(*monitor), NULL, (void*)&monitor);
	if (status != ferr_ok) {
		goto out;
	}

	simple_memset(monitor, 0, sizeof(*monitor));

	flock_mutex_init(&monitor->mutex);
	flock_semaphore_init(&monitor->triggered_items_semaphore, 0);

	frefcount_init(&monitor->refcount);

	monitor->next_item_id = 1;

	status = fproc_install_descriptor(fproc_current(), monitor, &fsyscall_monitor_descriptor_class, &monitor_handle);
	if (status != ferr_ok) {
		goto out;
	}

out:
	if (status == ferr_ok) {
		*out_monitor_handle = monitor_handle;
	}
	if (monitor) {
		fsyscall_monitor_release(monitor);
	}
	return status;
};

ferr_t fsyscall_handler_monitor_close(uint64_t monitor_handle) {
	fsyscall_monitor_t* monitor = NULL;
	const fproc_descriptor_class_t* desc_class = NULL;
	ferr_t status = ferr_ok;

	status = fproc_lookup_descriptor(fproc_current(), monitor_handle, true, (void*)&monitor, &desc_class);
	if (status != ferr_ok) {
		goto out;
	}

	if (desc_class != &fsyscall_monitor_descriptor_class) {
		status = ferr_invalid_argument;
		goto out;
	}

	status = fproc_uninstall_descriptor(fproc_current(), monitor_handle);
	if (status != ferr_ok) {
		goto out;
	}

	flock_mutex_lock(&monitor->mutex);

	monitor->flags |= fsyscall_monitor_flag_closed;

	for (size_t i = monitor->item_count; i < monitor->items_array_size; ++i) {
		fsyscall_monitor_item_t* item = monitor->items[i];
		monitor->items[i] = NULL;
		fsyscall_monitor_item_release(item);
	}

	for (size_t i = 0; i < monitor->item_count; ++i) {
		fsyscall_monitor_item_t* item = monitor->items[i];
		monitor->items[i] = NULL;
		fsyscall_monitor_item_delete(item);
		fsyscall_monitor_item_release(item);
	}

	if (monitor->items) {
		FERRO_WUR_IGNORE(fmempool_free(monitor->items));
	}

	// wake up everyone that's polling
	for (size_t i = 0; i < monitor->outstanding_polls; ++i) {
		flock_semaphore_up(&monitor->triggered_items_semaphore);
	}

	flock_mutex_unlock(&monitor->mutex);

out:
	if (monitor) {
		desc_class->release(monitor);
	}
	return status;
};

static fsyscall_monitor_item_flags_t fsyscall_update_flags_to_item_flags(fsyscall_monitor_update_item_flags_t update_flags) {
	fsyscall_monitor_item_flags_t flags = 0;

	if ((update_flags & fsyscall_monitor_update_item_flag_enabled) != 0) {
		flags |= fsyscall_monitor_item_flag_enabled;
	}

	if ((update_flags & fsyscall_monitor_update_item_flag_disable_on_trigger) != 0) {
		flags |= fsyscall_monitor_item_flag_disable_on_trigger;
	}

	if ((update_flags & fsyscall_monitor_update_item_flag_edge_triggered) != 0) {
		flags |= fsyscall_monitor_item_flag_edge_triggered;
	}

	if ((update_flags & fsyscall_monitor_update_item_flag_active_low) != 0) {
		flags |= fsyscall_monitor_item_flag_active_low;
	}

	if ((update_flags & fsyscall_monitor_update_item_flag_delete_on_trigger) != 0) {
		flags |= fsyscall_monitor_item_flag_delete_on_trigger;
	}

	if ((update_flags & fsyscall_monitor_update_item_flag_defer_delete) != 0) {
		flags |= fsyscall_monitor_item_flag_defer_delete;
	}

	if ((update_flags & fsyscall_monitor_update_item_flag_set_user_flag) != 0) {
		flags |= fsyscall_monitor_item_flag_set_user_flag;
	}

	return flags;
};

ferr_t fsyscall_handler_monitor_update(uint64_t monitor_handle, fsyscall_monitor_update_flags_t flags, fsyscall_monitor_update_item_t* in_out_items, uint64_t* in_out_item_count) {
	ferr_t status = ferr_ok;
	uint64_t item_count = *in_out_item_count;
	fsyscall_monitor_t* monitor = NULL;
	const fproc_descriptor_class_t* desc_class = NULL;
	uint64_t processed_items = 0;

	status = fproc_lookup_descriptor(fproc_current(), monitor_handle, true, (void*)&monitor, &desc_class);
	if (status != ferr_ok) {
		goto out;
	}

	if (desc_class != &fsyscall_monitor_descriptor_class) {
		status = ferr_invalid_argument;
		goto out;
	}

	for (size_t i = 0; i < item_count; ++i) {
		fsyscall_monitor_update_item_t* update_item = &in_out_items[i];
		fsyscall_monitor_item_t* item = NULL;
		ferr_t item_status = ferr_ok;
		bool create_flag = (update_item->flags & fsyscall_monitor_update_item_flag_create) != 0;
		bool update_flag = (update_item->flags & fsyscall_monitor_update_item_flag_update) != 0;
		bool delete_flag = (update_item->flags & fsyscall_monitor_update_item_flag_delete) != 0;
		bool deferred_delete = false;
		bool created = false;
		bool strict_match = (update_item->flags & fsyscall_monitor_update_item_flag_strict_match) != 0;

		if (
			// can't create/update it and also delete it simultaneously
			((create_flag || update_flag) && delete_flag)
		) {
			item_status = ferr_invalid_argument;
			goto item_out;
		}

		switch (update_item->header.type) {
			case fsyscall_monitor_item_type_channel:
			case fsyscall_monitor_item_type_server_channel:
				break;

			case fsyscall_monitor_item_type_futex:
				if (
					(create_flag || update_flag) &&
					(
						// futex items must be:
						// 1) edge triggered, and
						// 2) active high
						// (for now, at least)
						(update_item->flags & fsyscall_monitor_update_item_flag_edge_triggered) == 0 ||
						(update_item->flags & fsyscall_monitor_update_item_flag_active_low) != 0
					)
				) {
					item_status = ferr_invalid_argument;
					goto item_out;
				}
				break;

			case fsyscall_monitor_item_type_timeout:
				if (
					(create_flag || update_flag) &&
					(
						// timeout items must be:
						// 1) edge triggered, and
						// 2) active high
						// (for now, at least)
						(update_item->flags & fsyscall_monitor_update_item_flag_edge_triggered) == 0 ||
						(update_item->flags & fsyscall_monitor_update_item_flag_active_low) != 0
					)
				) {
					item_status = ferr_invalid_argument;
					goto item_out;
				}
				break;

			default:
				item_status = ferr_invalid_argument;
				goto item_out;
		}

		flock_mutex_lock(&monitor->mutex);

		if (create_flag && !update_flag) {
create_item:
			item_status = fmempool_reallocate(monitor->items, sizeof(*monitor->items) * (monitor->items_array_size + 1), NULL, (void*)&monitor->items);
			if (item_status != ferr_ok) {
				goto item_out;
			}

			item_status = fsyscall_monitor_item_create(&update_item->header, update_item->events, fsyscall_update_flags_to_item_flags(update_item->flags), monitor, update_item->data1, update_item->data2, &item);
			if (item_status != ferr_ok) {
				goto item_out;
			}

			simple_memmove(&monitor->items[monitor->item_count + 1], &monitor->items[monitor->item_count], (monitor->items_array_size - monitor->item_count) * sizeof(*monitor->items));

			monitor->items[monitor->item_count] = item;
			++monitor->item_count;
			++monitor->items_array_size;

			update_item->header.id = item->header.id;

			created = true;
		} else {
			item_status = ferr_no_such_resource;

			// an ID of "none" is never present in the monitor
			if (update_item->header.id == fsyscall_monitor_item_id_none) {
				if (create_flag) {
					// fall back to creating the item
					goto create_item;
				}

				goto item_out;
			}

			for (size_t i = 0; i < monitor->item_count; ++i) {
				fsyscall_monitor_item_t* this_item = monitor->items[i];

				if (
					this_item->header.id == update_item->header.id &&
					(
						!strict_match ||
						(
							this_item->header.type == update_item->header.type &&
							this_item->header.descriptor_id == update_item->header.descriptor_id &&
							this_item->header.context == update_item->header.context
						)
					)
				) {
					item = this_item;
					item_status = ferr_ok;

					if (delete_flag) {
						monitor->items[i] = NULL;
						simple_memmove(&monitor->items[i], &monitor->items[i + 1], ((monitor->items_array_size - i) - 1) * sizeof(*monitor->items));
						--monitor->item_count;

						if (
							(item->monitored_events & fsyscall_monitor_event_item_deleted) &&
							(
								(update_item->flags & fsyscall_monitor_update_item_flag_defer_delete) ||
								monitor->outstanding_polls > 0
							)
						) {
							// we're deleting this item, but not just yet.
							// mark it as dead but keep it alive until someone polls it and sees the death event.
							deferred_delete = true;

							// we delete the item here to remove event listeners/waiters, but we do NOT release it
							fsyscall_monitor_item_delete(item);

							item->flags |= fsyscall_monitor_item_flag_dead;
							item->triggered_events |= fsyscall_monitor_event_item_deleted;

							monitor->items[monitor->items_array_size - 1] = item;

							flock_semaphore_up(&monitor->triggered_items_semaphore);
						} else {
							// we're deleting this item;
							// remove it from the list
							--monitor->items_array_size;

							// now shrink the list;
							// we don't care it if it fails, this is only an optimization for reducing memory usage
							FERRO_WUR_IGNORE(fmempool_reallocate(monitor->items, sizeof(*monitor->items) * monitor->items_array_size, NULL, (void*)&monitor->items));
						}
					}

					break;
				}
			}

			if (item_status != ferr_ok) {
				if (create_flag) {
					// fall back to creating the item if we didn't find any to merge with
					goto create_item;
				}

				goto item_out;
			}
		}

		if (update_flag) {
			fsyscall_monitor_item_flags_t flags = item->flags;
			fsyscall_monitor_item_flags_t old_flags = flags;

			flags &= ~(fsyscall_monitor_item_flag_enabled | fsyscall_monitor_item_flag_disable_on_trigger | fsyscall_monitor_item_flag_edge_triggered | fsyscall_monitor_item_flag_active_low | fsyscall_monitor_item_flag_delete_on_trigger | fsyscall_monitor_item_flag_defer_delete | fsyscall_monitor_item_flag_set_user_flag);
			flags |= fsyscall_update_flags_to_item_flags(update_item->flags);
			item->flags = flags;

			item->header.context = update_item->header.context;

			item->monitored_events = update_item->events;

			if (item->header.type == fsyscall_monitor_item_type_futex) {
				fsyscall_monitor_item_futex_t* futex_item = (void*)item;
				futex_item->expected_value = create_flag ? update_item->data2 : update_item->data1;
			}

			if ((old_flags & fsyscall_monitor_item_flag_enabled) != 0 && (item->flags & fsyscall_monitor_item_flag_enabled) == 0) {
				// it was enabled, but now needs to be disabled
				fsyscall_monitor_item_disable(item);
			} else if ((old_flags & fsyscall_monitor_item_flag_enabled) == 0 && (item->flags & fsyscall_monitor_item_flag_enabled) != 0) {
				// it was disabled, but now needs to be enabled
				fsyscall_monitor_item_enable(item);
			}

			if ((old_flags & fsyscall_monitor_item_flag_active_low) != (flags & fsyscall_monitor_item_flag_active_low)) {
				// we're switching activation sensitivity, so we need to invert the triggered events bitset
				item->triggered_events = ~item->triggered_events;

				// now let's force someone polling the monitor to re-check
				flock_semaphore_up(&monitor->triggered_items_semaphore);
			}
		}

item_out:
		flock_mutex_unlock(&monitor->mutex);

		if (item_status == ferr_ok) {
			if (delete_flag && !deferred_delete) {
				// we're deleting, let's do it now outside the lock
				//
				// we don't need to do it outside the lock, but we also don't need to do it inside the lock,
				// so let's do it outside to avoid holding the lock for extended periods of time
				fsyscall_monitor_item_delete(item);
				fsyscall_monitor_item_release(item);
			}
		}

		update_item->status = item_status;

		++processed_items;

		if (item_status != ferr_ok && (flags & fsyscall_monitor_update_flag_fail_fast) != 0) {
			goto out;
		}
	}

out:
	if (monitor) {
		desc_class->release(monitor);
	}
	*in_out_item_count = processed_items;
	return status;
};

ferr_t fsyscall_handler_monitor_poll(uint64_t monitor_handle, fsyscall_monitor_poll_flags_t flags, uint64_t timeout, fsyscall_timeout_type_t timeout_type, fsyscall_monitor_event_t* out_events, uint64_t* in_out_event_count) {
	ferr_t status = ferr_ok;
	fsyscall_monitor_t* monitor = NULL;
	const fproc_descriptor_class_t* desc_class = NULL;
	uint64_t event_array_size = *in_out_event_count;
	uint64_t processed_events = 0;
	bool marked_outstanding = false;

	if (event_array_size == 0) {
		status = ferr_invalid_argument;
		goto out;
	}

	status = fproc_lookup_descriptor(fproc_current(), monitor_handle, true, (void*)&monitor, &desc_class);
	if (status != ferr_ok) {
		goto out;
	}

	if (desc_class != &fsyscall_monitor_descriptor_class) {
		status = ferr_invalid_argument;
		goto out;
	}

	// TODO: implement actual timeouts
	switch (timeout_type) {
		case fsyscall_timeout_type_none:
			break;

		default:
			if (timeout != 0) {
				status = ferr_unsupported;
				goto out;
			}
			break;
	}

	flock_mutex_lock(&monitor->mutex);

	if (monitor->flags & fsyscall_monitor_flag_closed) {
		// we're being closed. don't start polling now.
		flock_mutex_unlock(&monitor->mutex);
		status = ferr_permanent_outage;
		goto out;
	}

	++monitor->outstanding_polls;
	flock_mutex_unlock(&monitor->mutex);
	marked_outstanding = true;

	while (true) {
		if (timeout_type == fsyscall_timeout_type_none) {
			flock_semaphore_down(&monitor->triggered_items_semaphore);
		} else {
			// assumes timeout of 0
			if (flock_semaphore_try_down(&monitor->triggered_items_semaphore) != ferr_ok) {
				status = ferr_timed_out;
				goto out;
			}
		}

		flock_mutex_lock(&monitor->mutex);

		if (monitor->flags & fsyscall_monitor_flag_closed) {
			// we were woken up because we're being closed.
			// stop looking for events now.
			flock_mutex_unlock(&monitor->mutex);
			status = ferr_permanent_outage;
			break;
		}

		// check dead items first; we want to remove them all
		for (size_t i = monitor->item_count; i < monitor->items_array_size; ++i) {
			if (processed_events >= event_array_size) {
				// can't process any more
				break;
			}
			if (fsyscall_monitor_item_poll(monitor->items[i], &out_events[processed_events]) == ferr_ok) {
				if ((monitor->items[i]->flags & fsyscall_monitor_item_flag_dead) == 0) {
					fpanic("Trying to release living item?");
				}

				++processed_events;

				// release it now
				fsyscall_monitor_item_release(monitor->items[i]);
			}
		}

		// remove all the processed dead items from the array
		simple_memmove(&monitor->items[monitor->item_count], &monitor->items[monitor->item_count + processed_events], (monitor->items_array_size - (monitor->item_count + processed_events)) * sizeof(*monitor->items));
		monitor->items_array_size -= processed_events;

		// now shrink the list;
		// we don't care it if it fails, this is only an optimization for reducing memory usage
		FERRO_WUR_IGNORE(fmempool_reallocate(monitor->items, sizeof(*monitor->items) * monitor->items_array_size, NULL, (void*)&monitor->items));

		// now check living items
		for (size_t i = 0; i < monitor->item_count; ++i) {
			fsyscall_monitor_item_t* item = monitor->items[i];
			if (processed_events >= event_array_size) {
				// can't process any more
				break;
			}
			if (fsyscall_monitor_item_poll(item, &out_events[processed_events]) == ferr_ok) {
				if (item->flags & fsyscall_monitor_item_flag_disable_on_trigger) {
					fsyscall_monitor_item_disable(item);
				}
				if (item->flags & fsyscall_monitor_item_flag_delete_on_trigger) {
					monitor->items[i] = NULL;
					simple_memmove(&monitor->items[i], &monitor->items[i + 1], ((monitor->items_array_size - i) - 1) * sizeof(*monitor->items));
					--monitor->item_count;
					--monitor->items_array_size;

					// check this index again on the next iteration
					--i;

					fsyscall_monitor_item_delete(item);
					fsyscall_monitor_item_release(item);
				}
				++processed_events;
			}
		}

		flock_mutex_unlock(&monitor->mutex);

		if (processed_events > 0) {
			// once we have at least one event, we can return
			break;
		}
	}

out:
	if (monitor) {
		if (marked_outstanding) {
			flock_mutex_lock(&monitor->mutex);
			--monitor->outstanding_polls;
			flock_mutex_unlock(&monitor->mutex);
		}
		desc_class->release(monitor);
	}
	*in_out_event_count = processed_events;
	return status;
};
