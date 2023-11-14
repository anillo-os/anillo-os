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

static void eve_loop_destroy(eve_object_t* obj) {
	sys_object_destroy(obj);
};

static ferr_t eve_loop_retain_noop(eve_loop_t* obj) {
	return ferr_ok;
};

static void eve_loop_release_noop(eve_loop_t* obj) {
	// do nothing
};

const eve_object_class_t eve_loop_class = {
	LIBSYS_OBJECT_CLASS_INTERFACE(NULL),
	.destroy = eve_loop_destroy,
	.retain = eve_loop_retain_noop,
	.release = eve_loop_release_noop,
};

const eve_object_class_t* eve_object_class_loop(void) {
	return &eve_loop_class;
};

static eve_loop_t dummy_loop = {
	.reference_count = 1,
	.object_class = &eve_loop_class,
	.flags = 0,
};

eve_loop_t* eve_loop_get_main(void) {
	return &dummy_loop;
};

eve_loop_t* eve_loop_get_current(void) {
	return &dummy_loop;
};

ferr_t eve_loop_add_item(eve_loop_t* loop, eve_item_t* item) {
	return eve_retain(item);
};

ferr_t eve_loop_remove_item(eve_loop_t* loop, eve_item_t* item) {
	eve_release(item);
	return ferr_ok;
};

void eve_loop_run(eve_loop_t* loop) {
	// no-op
	while (true);
};

void eve_loop_run_one(eve_loop_t* loop) {
	// no-op
};

void eve_mutex_lock(sys_mutex_t* mutex) {
	sys_mutex_lock(mutex);
};

void eve_semaphore_down(sys_semaphore_t* semaphore) {
	sys_semaphore_down(semaphore);
};

void eve_event_wait(sys_event_t* event) {
	sys_event_wait(event);
};

void eve_once(sys_once_t* token, sys_once_f initializer, void* context, sys_once_flags_t flags) {
	return sys_once(token, initializer, context, flags);
};

//
// unsupported APIs
//

ferr_t eve_loop_create(eve_loop_t** out_loop) {
	return ferr_unsupported;
};

ferr_t eve_loop_enqueue(eve_loop_t* loop, eve_loop_work_f work, void* context) {
	return ferr_unsupported;
};

ferr_t eve_loop_schedule(eve_loop_t* loop, eve_loop_work_f work, void* context, uint64_t timeout, sys_timeout_type_t timeout_type, eve_loop_work_id_t* out_id) {
	return ferr_unsupported;
};

ferr_t eve_loop_cancel(eve_loop_t* loop, eve_loop_work_id_t id) {
	return ferr_unsupported;
};

ferr_t eve_loop_suspend_current(eve_loop_t* loop, eve_loop_suspension_callback_f suspension_callback, void* context, eve_loop_work_id_t* out_id) {
	return ferr_unsupported;
};

ferr_t eve_loop_resume(eve_loop_t* loop, eve_loop_work_id_t id) {
	return ferr_unsupported;
};
