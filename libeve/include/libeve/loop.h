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

#ifndef _LIBEVE_LOOP_H_
#define _LIBEVE_LOOP_H_

#include <libeve/base.h>
#include <libeve/objects.h>
#include <libeve/item.h>

LIBEVE_DECLARATIONS_BEGIN;

LIBEVE_OBJECT_CLASS(loop);

typedef void (*eve_loop_work_f)(void* context);
typedef void (*eve_loop_suspension_callback_f)(void* context);

LIBEVE_ENUM(uint64_t, eve_loop_work_id) {
	eve_loop_work_id_invalid = 0,
};

eve_loop_t* eve_loop_get_main(void);
eve_loop_t* eve_loop_get_current(void);

LIBEVE_WUR ferr_t eve_loop_create(eve_loop_t** out_loop);

LIBEVE_WUR ferr_t eve_loop_add_item(eve_loop_t* loop, eve_item_t* item);
LIBEVE_WUR ferr_t eve_loop_remove_item(eve_loop_t* loop, eve_item_t* item);

void eve_loop_run(eve_loop_t* loop);
void eve_loop_run_one(eve_loop_t* loop);

/**
 * Schedule a work item to run on the loop.
 *
 * Note that, by default, work items have a stack size of 512 KiB. Threads typically get
 * 2 MiB of stack space, but work items aren't supposed to need that much. If you need more
 * stack space for your work item, you can configure the loop to allocate more stack space for
 * work items.
 *
 * However, chances are that if you need more than 512 KiB, you probably shouldn't be using work
 * items; at the very least, you should consider breaking up the work into smaller chunks.
 * Alternatively, consider using a dedicated thread to perform the work instead.
 */
LIBEVE_WUR ferr_t eve_loop_enqueue(eve_loop_t* loop, eve_loop_work_f work, void* context);
LIBEVE_WUR ferr_t eve_loop_schedule(eve_loop_t* loop, eve_loop_work_f work, void* context, uint64_t timeout, sys_timeout_type_t timeout_type, eve_loop_work_id_t* out_id);
LIBEVE_WUR ferr_t eve_loop_cancel(eve_loop_t* loop, eve_loop_work_id_t id);
LIBEVE_WUR ferr_t eve_loop_suspend_current(eve_loop_t* loop, eve_loop_suspension_callback_f suspension_callback, void* context, eve_loop_work_id_t* out_id);
LIBEVE_WUR ferr_t eve_loop_resume(eve_loop_t* loop, eve_loop_work_id_t id);

LIBEVE_DECLARATIONS_END;

#endif // _LIBEVE_LOOP_H_
