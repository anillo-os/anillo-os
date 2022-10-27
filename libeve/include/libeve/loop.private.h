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

#ifndef _LIBEVE_LOOP_PRIVATE_H_
#define _LIBEVE_LOOP_PRIVATE_H_

#include <libeve/loop.h>

#include <libsys/libsys.h>
#include <libeve/objects.private.h>
#include <libsimple/ring.h>

LIBEVE_DECLARATIONS_BEGIN;

LIBEVE_STRUCT(eve_loop_work_item) {
	eve_loop_work_id_t id;
	eve_loop_work_f work;
	void* context;
	void* stack;
	sys_ucs_context_t ucs_context;
	eve_loop_suspension_callback_f suspension_callback;
	void* suspension_context;
};

LIBEVE_STRUCT(eve_loop_object) {
	sys_object_t object;
	sys_monitor_t* monitor;
	sys_counter_t* death_counter;
	sys_thread_t* polling_thread;
	size_t item_count;
	eve_object_t** items;
	sys_mutex_t mutex;
	simple_ring_t ring;
	bool ring_inited;
	eve_loop_work_item_t ring_buffer[64];
	sys_semaphore_t work_semaphore;
	eve_loop_work_id_t next_id;

	sys_mutex_t suspended_work_mutex;
	size_t suspended_work_count;
	eve_loop_work_item_t* suspended_work;
};

LIBEVE_DECLARATIONS_END;

#endif // _LIBEVE_LOOP_PRIVATE_H_
