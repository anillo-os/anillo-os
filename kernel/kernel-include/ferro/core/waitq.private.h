/*
 * This file is part of Anillo OS
 * Copyright (C) 2021 Anillo OS Developers
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

/**
 * @file
 *
 * waitq subsystem; private components.
 */

#ifndef _FERRO_CORE_WAITQ_PRIVATE_H_
#define _FERRO_CORE_WAITQ_PRIVATE_H_

#include <ferro/base.h>
#include <ferro/core/waitq.h>

FERRO_DECLARATIONS_BEGIN;

/**
 * @addtogroup Waitq
 *
 * @{
 */

void fwaitq_lock(fwaitq_t* waitq);

void fwaitq_unlock(fwaitq_t* waitq);

void fwaitq_add_locked(fwaitq_t* waitq, fwaitq_waiter_t* waiter);

void fwaitq_remove_locked(fwaitq_t* waitq, fwaitq_waiter_t* waiter);

/**
 * Like fwaitq_wake_many(), but enters with the waitq already locked.
 *
 * @note This function MUST drop the lock before calling any wakeup callbacks and reacquire it afterwards.
 *       It returns with the lock held.
 */
void fwaitq_wake_many_locked(fwaitq_t* waitq, size_t count);

bool fwaitq_empty_locked(fwaitq_t* waitq);

/**
 * @}
 */

FERRO_DECLARATIONS_END;

#endif // _FERRO_CORE_WAITQ_PRIVATE_H_

