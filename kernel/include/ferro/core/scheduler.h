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
 * Scheduler subsystem.
 */

#ifndef _FERRO_CORE_SCHEDULER_H_
#define _FERRO_CORE_SCHEDULER_H_

#include <ferro/base.h>
#include <ferro/platform.h>
#include <ferro/core/threads.h>

FERRO_DECLARATIONS_BEGIN;

/**
 * @addtogroup Scheduler
 *
 * The scheduler subsystem.
 *
 * @{
 */

/**
 * Initializes the scheduler subsystem.
 *
 * @param thread The thread to start executing.
 *
 * @note After this, all kernel code runs in the context of a thread (or an interrupt).
 *
 * @note The given thread is automatically resumed (if necessary).
 *
 * @note This function transfers ownership of the caller's reference into the scheduler subsystem (i.e. it doesn't retain the thread; it takes the caller's reference).
 */
FERRO_NO_RETURN void fsched_init(fthread_t* thread);

/**
 * Asks the scheduler to beging managing the given thread.
 *
 * @param in_out_thread The thread to begin managing. A new, unique thread ID is assigned to this thread.
 *
 * @note This function will suspend the thread. In order for it to begin running afterwards, it must be explicitly resumed.
 *
 * @note This function will retain the thread. When the thread dies, it will be released.
 *
 * @todo Allow the thread to be unmanaged by the scheduler. Right now, the only way to do that is to kill it.
 *
 * Return values:
 * @retval ferr_ok                  The thread is now suspended and being managed by the scheduler subsystem.
 * @retval ferr_already_in_progress The thread was already being managed by the scheduler subsystem.
 * @retval ferr_temporary_outage    There were insufficient resources to begin managing the thread.
 * @retval ferr_invalid_argument    The threas was `NULL`, dead/dying, or fully released just before it could be retained.
 */
ferr_t fsched_manage(fthread_t* in_out_thread);

/**
 * @}
 */

FERRO_DECLARATIONS_END;

#endif // _FERRO_CORE_SCHEDULER_H_
