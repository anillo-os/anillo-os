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

#ifndef _LIBSYS_MONITORS_PRIVATE_H_
#define _LIBSYS_MONITORS_PRIVATE_H_

#include <libsys/monitors.h>
#include <libsys/objects.private.h>
#include <gen/libsyscall/syscall-wrappers.h>
#include <libsys/locks.h>

LIBSYS_DECLARATIONS_BEGIN;

#define SYS_MONITOR_DID_INVALID UINT64_MAX

LIBSYS_STRUCT_FWD(sys_monitor_object);

LIBSYS_STRUCT(sys_monitor_item_object) {
	sys_object_t object;

	sys_object_t* target;

	sys_mutex_t mutex;

	// TODO: allow monitor items to be shared between multiple monitors
	sys_monitor_t* monitor;
	uint64_t id;

	sys_monitor_item_flags_t flags;
	sys_monitor_events_t events;
	void* context;
};

LIBSYS_STRUCT(sys_monitor_object) {
	sys_object_t object;
	uint64_t monitor_did;
	sys_mutex_t mutex;
	sys_monitor_item_t** items;
	size_t item_count;
	size_t array_size;
	size_t outstanding_polls;
};

uint64_t sys_monitor_item_descriptor_id(sys_monitor_item_object_t* item);
libsyscall_monitor_item_type_t sys_monitor_item_type(sys_monitor_item_object_t* item);

LIBSYS_ALWAYS_INLINE
libsyscall_monitor_events_t sys_monitor_events_to_libsyscall_monitor_events(sys_monitor_events_t events) {
	// for now, we just keep the bits in sync with each other, so there's no need to translate anything
	return events;
};

LIBSYS_ALWAYS_INLINE
sys_monitor_events_t libsyscall_monitor_events_to_sys_monitor_events(libsyscall_monitor_events_t events) {
	return events;
};

LIBSYS_ALWAYS_INLINE
libsyscall_monitor_update_item_flags_t sys_monitor_item_flags_to_libsyscall_monitor_update_item_flags(sys_monitor_item_flags_t flags) {
	libsyscall_monitor_update_item_flags_t result = 0;
	if (flags & sys_monitor_item_flag_level_triggered) {
		result |= libsyscall_monitor_update_item_flag_level_triggered;
	}
	if (flags & sys_monitor_item_flag_edge_triggered) {
		result |= libsyscall_monitor_update_item_flag_edge_triggered;
	}
	if (flags & sys_monitor_item_flag_active_high) {
		result |= libsyscall_monitor_update_item_flag_active_high;
	}
	if (flags & sys_monitor_item_flag_active_low) {
		result |= libsyscall_monitor_update_item_flag_active_low;
	}
	if (flags & sys_monitor_item_flag_enabled) {
		result |= libsyscall_monitor_update_item_flag_enabled;
	}
	if (flags & sys_monitor_item_flag_disable_on_trigger) {
		result |= libsyscall_monitor_update_item_flag_disable_on_trigger;
	}
	return result;
};

LIBSYS_DECLARATIONS_END;

#endif // _LIBSYS_MONITORS_PRIVATE_H_
