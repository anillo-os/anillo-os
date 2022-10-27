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

#ifndef _LIBEVE_MUTEX_H_
#define _LIBEVE_MUTEX_H_

#include <libeve/base.h>
#include <libeve/loop.h>

LIBEVE_DECLARATIONS_BEGIN;

void eve_mutex_lock(sys_mutex_t* mutex);
void eve_semaphore_down(sys_semaphore_t* semaphore);
void eve_event_wait(sys_event_t* event);

LIBEVE_DECLARATIONS_END;

#endif // _LIBEVE_MUTEX_H_
