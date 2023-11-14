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

#ifndef _FERRO_SYSCALLS_MONITORS_PRIVATE_H_
#define _FERRO_SYSCALLS_MONITORS_PRIVATE_H_

#include <stdint.h>
#include <stddef.h>

#include <ferro/base.h>
#include <ferro/core/refcount.h>
#include <gen/ferro/userspace/syscall-handlers.h>
#include <ferro/core/locks.h>
#include <ferro/core/channels.h>
#include <ferro/userspace/futex.h>
#include <ferro/core/workers.h>

FERRO_DECLARATIONS_BEGIN;

FERRO_STRUCT_FWD(fsyscall_monitor_item);

FERRO_ENUM(uint64_t, fsyscall_monitor_flags) {
	fsyscall_monitor_flag_closed = 1 << 0,
};

FERRO_STRUCT(fsyscall_monitor) {
	frefcount_t refcount;
	fsyscall_monitor_item_t** items;
	// FIXME: this would be better as a condition variable, but we don't currently have those.
	//        and no, waitqs don't count; you can't atomically unlock a mutex and wait with a waitq
	//        (at least not with the current API).
	//
	// at least with a semaphore, we can't miss wakeups.
	flock_semaphore_t triggered_items_semaphore;
	flock_mutex_t mutex;
	size_t item_count;
	size_t items_array_size;
	uint64_t next_item_id;
	size_t outstanding_polls;
	fsyscall_monitor_flags_t flags;
};

FERRO_ENUM(uint64_t, fsyscall_monitor_item_flags) {
	fsyscall_monitor_item_flag_enabled            = 1 << 0,
	fsyscall_monitor_item_flag_disable_on_trigger = 1 << 1,
	fsyscall_monitor_item_flag_edge_triggered     = 1 << 2,
	fsyscall_monitor_item_flag_active_low         = 1 << 3,
	fsyscall_monitor_item_flag_keep_alive         = 1 << 4,
	fsyscall_monitor_item_flag_dead               = 1 << 5,
	fsyscall_monitor_item_flag_delete_on_trigger  = 1 << 6,
	fsyscall_monitor_item_flag_defer_delete       = 1 << 7,
	fsyscall_monitor_item_flag_set_user_flag      = 1 << 8,
};

// TODO: "edge vs. level triggered" and "active high vs low" should be configurable per-event rather than per-item.
//       this can be worked around for now because we allow multiple monitor items to be created for the same descriptor,
//       so users can setup separate items with different settings.

FERRO_STRUCT(fsyscall_monitor_item) {
	frefcount_t refcount;
	fsyscall_monitor_item_header_t header;
	fsyscall_monitor_item_flags_t flags;
	fsyscall_monitor_events_t monitored_events;
	fsyscall_monitor_events_t triggered_events;
	fsyscall_monitor_t* monitor;
};

FERRO_STRUCT(fsyscall_monitor_item_channel) {
	fsyscall_monitor_item_t base;

	fchannel_t* channel;

	bool message_arrival_high;
	fwaitq_waiter_t message_arrival_waiter;

	bool queue_empty_high;
	fwaitq_waiter_t queue_empty_waiter;

	bool peer_queue_empty_high;
	fwaitq_waiter_t peer_queue_empty_waiter;
	fwaitq_waiter_t peer_message_arrival_waiter;

	bool peer_close_high;
	fwaitq_waiter_t peer_close_waiter;

	bool peer_queue_space_available_high;
	fwaitq_waiter_t peer_queue_removal_waiter;
	fwaitq_waiter_t peer_queue_full_waiter;

	bool close_high;
	fwaitq_waiter_t close_waiter;
};

FERRO_STRUCT(fsyscall_monitor_item_futex) {
	fsyscall_monitor_item_t base;
	futex_t* futex;
	fwaitq_waiter_t waiter;
	uint64_t expected_value;
};

FERRO_STRUCT(fsyscall_monitor_item_timeout) {
	fsyscall_monitor_item_t base;
	fwork_t* work;
};

FERRO_DECLARATIONS_END;

#endif // _FERRO_SYSCALLS_MONITORS_PRIVATE_H_
