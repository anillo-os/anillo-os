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

#ifndef _LIBSYS_MONITORS_H_
#define _LIBSYS_MONITORS_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include <libsys/base.h>
#include <libsys/objects.h>
#include <libsys/timeout.h>

LIBSYS_DECLARATIONS_BEGIN;

LIBSYS_OBJECT_CLASS(monitor);
LIBSYS_OBJECT_CLASS(monitor_item);

LIBSYS_ENUM(uint64_t, sys_monitor_item_flags) {
	sys_monitor_item_flag_level_triggered = 0 << 0,
	sys_monitor_item_flag_edge_triggered  = 1 << 0,
	sys_monitor_item_flag_active_high     = 0 << 1,
	sys_monitor_item_flag_active_low      = 1 << 1,
	sys_monitor_item_flag_enabled         = 1 << 2,
	sys_monitor_item_flag_disable_on_trigger = 1 << 3,
};

LIBSYS_ENUM(uint64_t, sys_monitor_events) {
	sys_monitor_event_item_deleted                          = 1 << 0,

	sys_monitor_event_channel_message_arrived               = 1 << 1,
	sys_monitor_event_channel_queue_emptied                 = 1 << 2,
	sys_monitor_event_channel_peer_queue_emptied            = 1 << 3,
	sys_monitor_event_channel_peer_closed                   = 1 << 4,
	sys_monitor_event_channel_peer_queue_space_available    = 1 << 5,

	sys_monitor_event_server_channel_client_arrived = 1 << 1,

	sys_monitor_event_counter_updated = 1 << 1,
};

LIBSYS_ENUM(uint64_t, sys_monitor_poll_flags) {
	sys_monitor_poll_flag_reserved = 0,
};

LIBSYS_ENUM(uint8_t, sys_monitor_poll_item_type) {
	sys_monitor_poll_item_type_item = 1,
	sys_monitor_poll_item_type_futex = 2,
	sys_monitor_poll_item_type_timeout = 3,
};

LIBSYS_STRUCT(sys_monitor_poll_item) {
	union {
		struct {
			sys_monitor_item_t* item;
			sys_monitor_events_t events;
		};
		struct {
			void* futex_context;
		};
		struct {
			void* timeout_context;
		};
	};
	sys_monitor_poll_item_type_t type;
};

LIBSYS_WUR ferr_t sys_monitor_create(sys_monitor_t** out_monitor);

LIBSYS_WUR ferr_t sys_monitor_item_create(sys_object_t* object, sys_monitor_item_flags_t flags, sys_monitor_events_t events, void* context, sys_monitor_item_t** out_item);
LIBSYS_WUR ferr_t sys_monitor_item_modify(sys_monitor_item_t* item, sys_monitor_item_flags_t flags, sys_monitor_events_t events, void* context, void** out_old_context);

LIBSYS_WUR sys_object_t* sys_monitor_item_target(sys_monitor_item_t* item);
LIBSYS_WUR void* sys_monitor_item_context(sys_monitor_item_t* item);

void sys_monitor_item_remove_from_all(sys_monitor_item_t* item, bool defer_deletion);

LIBSYS_WUR ferr_t sys_monitor_add_item(sys_monitor_t* monitor, sys_monitor_item_t* item);
LIBSYS_WUR ferr_t sys_monitor_remove_item(sys_monitor_t* monitor, sys_monitor_item_t* item, bool defer_deletion);
LIBSYS_WUR ferr_t sys_monitor_oneshot_futex(sys_monitor_t* monitor, uint64_t* address, uint64_t channel, uint64_t expected_value, void* context);
LIBSYS_WUR ferr_t sys_monitor_oneshot_timeout(sys_monitor_t* monitor, uint64_t timeout, sys_timeout_type_t timeout_type, void* context);

LIBSYS_WUR ferr_t sys_monitor_poll(sys_monitor_t* monitor, sys_monitor_poll_flags_t flags, uint64_t timeout, sys_timeout_type_t timeout_type, sys_monitor_poll_item_t* out_items, size_t* in_out_item_count);

LIBSYS_DECLARATIONS_END;

#endif // _LIBSYS_MONITORS_H_
