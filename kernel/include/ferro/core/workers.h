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
 * Workers subsystem.
 */

#ifndef _FERRO_CORE_WORKERS_H_
#define _FERRO_CORE_WORKERS_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include <ferro/base.h>
#include <ferro/error.h>

FERRO_DECLARATIONS_BEGIN;

/**
 * @addtogroup Workers
 *
 * The workers subsystem.
 *
 * @{
 */

typedef void (*fworker_f)(void* data);

FERRO_STRUCT_FWD(fworker);

/**
 * Initializes the workers subsystem.
 */
void fworkers_init(void);

/**
 * Creates a new worker instance for the given worker function and data.
 *
 * @param worker_function The worker function to run.
 * @param data            Optional user-defined data to pass to the worker function.
 * @param[out] out_worker      Pointer in which a pointer to the newly created worker instance will be placed.
 *
 * @note The worker instance structure is an opaque pointer. It is managed through reference counting using fworker_retain() and fworker_release().
 *       The caller of this function receives a new worker instance with a single reference.
 *
 * @note This does NOT schedule the worker instance to run. For that, use fworker_schedule().
 *
 * Return values:
 * @retval ferr_ok               The worker instance was successfully created.
 * @retval ferr_invalid_argument One or more of: 1) the worker function was `NULL`, 2) @p out_worker was `NULL`.
 * @retval ferr_temporary_outage There were insufficient resources to create a new worker instance.
 */
FERRO_WUR ferr_t fworker_new(fworker_f worker_function, void* data, fworker_t** out_worker);

/**
 * Tries to retain the given worker instance.
 *
 * @param thread The worker instance to try to retain.
 *
 * Return values:
 * @retval ferr_ok               The worker instance was successfully retained.
 * @retval ferr_permanent_outage The worker instance was deallocated while this call occurred. It is no longer valid.
 */
FERRO_WUR ferr_t fworker_retain(fworker_t* worker);

/**
 * Releases the given thread.
 *
 * @param thread The worker instance to release.
 */
void fworker_release(fworker_t* worker);

/**
 * Schedules then given worker instance to run on a worker thread sometime in the future.
 *
 * @param worker The worker instance to schedule.
 *
 * Return values:
 * @retval ferr_ok               The worker was successfully scheduled.
 * @retval ferr_invalid_argument One or more of: 1) the worker instance was `NULL`, 2) the worker instance was already scheduled.
 * @retval ferr_permanent_outage Retaining the worker failed.
 * @retval ferr_temporary_outage There were insufficient resources to schedule a new worker instance.
 */
FERRO_WUR ferr_t fworker_schedule(fworker_t* worker);

/**
 * Cancels the given worker instance if it hasn't started running yet.
 *
 * @param id The worker instance to cancel.
 *
 * @note This function CANNOT stop a worker instance that's already running.
 *
 * Return values:
 * @retval ferr_ok                  The worker instance was successfully cancelled.
 * @retval ferr_invalid_argument    The worker instance was `NULL`.
 * @retval ferr_already_in_progress The worker instance was already running and could not be cancelled. This is also returned if the worker has already completed.
 */
FERRO_WUR ferr_t fworker_cancel(fworker_t* worker);

/**
 * Waits for the given worker instance to complete.
 *
 * @param worker The worker instance to wait for.
 *
 * @note If the worker has already completed, this function will return immediately.
 *
 * Return values:
 * @retval ferr_ok The worker instance has successfully completed.
 */
void fworker_wait(fworker_t* worker);

/**
 * @}
 */

FERRO_DECLARATIONS_END;

#endif // _FERRO_CORE_WORKERS_H_

