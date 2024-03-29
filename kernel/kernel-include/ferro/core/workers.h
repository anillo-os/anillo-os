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

FERRO_STRUCT_FWD(fwork);

FERRO_OPTIONS(uint8_t, fwork_flags) {
	/**
	 * Allows work to be scheduled to run again even while it is running.
	 *
	 * This makes it possible for workers to run spuriously, but it also eliminates the chances
	 * of racing against a worker finishing up but still running.
	 *
	 * If this flag is set, fwork_schedule() and fwork_cancel() do not fail if the work is already running.
	 */
	fwork_flag_allow_reschedule = 1 << 0,

	fwork_flag_xxx_repeated_reschedule_bit = 1 << 1,
	fwork_flag_xxx_balanced_reschedule_bit = 1 << 2,

	/**
	 * Tracks how many times work has been rescheduled and reschedules it that many times.
	 *
	 * Normally, ::fwork_flag_allow_reschedule will only track a single reschedule.
	 * This means that if you call fwork_schedule() twice while the work is running,
	 * it will only be rescheduled to run once. Often, this is what you want;
	 * your worker should check how much it has to process and take care of it in a single run.
	 * However, sometimes you would like it to run as many times as you schedule it.
	 *
	 * One important distinction in behavior between plain ::fwork_flag_allow_reschedule and this flag
	 * is that with a plain allow-reschedule, cancelling a reschedule once cancels it completely.
	 * However, cancelling with this flag is balanced with the number of times you've rescheduled the work.
	 *
	 * Continuing the earlier example, if you've rescheduled it twice but then cancelled it once,
	 * the reschedule is cancelled. However, with this flag, you must cancel the work as many times
	 * as you've rescheduled it, so you would have to cancel it twice in the example.
	 *
	 * Implies ::fwork_flag_allow_reschedule.
	 */
	fwork_flag_repeated_reschedule = fwork_flag_xxx_repeated_reschedule_bit | fwork_flag_allow_reschedule,

	/**
	 * Allows you to balance reschedules with cancellations but only run the rescheduled work once.
	 *
	 * This flag is similar to ::fwork_flag_repeated_reschedule in that it tracks how many times you've
	 * rescheduled work and requires you to cancel it the same number of times in order to properly cancel a reschedule.
	 *
	 * The difference lies in the fact that, once the work finishes running and will actually be rescheduled,
	 * it is only scheduled once. After it has been scheduled once, the reschedule counter resets to 0.
	 *
	 * Implies ::fwork_flag_allow_reschedule.
	 */
	fwork_flag_balanced_reschedule = fwork_flag_xxx_balanced_reschedule_bit | fwork_flag_allow_reschedule,
};

/**
 * Initializes the workers subsystem.
 */
void fworkers_init(void);

/**
 * Creates a new work instance for the given worker function and data.
 *
 * @param worker_function The worker function to run.
 * @param            data Optional user-defined data to pass to the worker function.
 * @param[out]   out_work Pointer in which a pointer to the newly created work instance will be placed.
 *
 * @note The work instance structure is an opaque pointer. It is managed through reference counting using fwork_retain() and fwork_release().
 *       The caller of this function receives a new work instance with a single reference.
 *
 * @note This does NOT schedule the work instance to run. For that, use fwork_schedule().
 *       Alternatively, to create a new work instance and schedule it at the same time, use fworker_schedule_new().
 *
 * Return values:
 * @retval ferr_ok               The work instance was successfully created.
 * @retval ferr_invalid_argument One or more of: 1) the worker function was `NULL`, 2) @p out_work was `NULL`.
 * @retval ferr_temporary_outage There were insufficient resources to create a new work instance.
 */
FERRO_WUR ferr_t fwork_new(fworker_f worker_function, void* data, fwork_flags_t flags, fwork_t** out_worker);

/**
 * Tries to retain the given work instance.
 *
 * @param thread The work instance to try to retain.
 *
 * Return values:
 * @retval ferr_ok               The work instance was successfully retained.
 * @retval ferr_permanent_outage The work instance was deallocated while this call occurred. It is no longer valid.
 */
FERRO_WUR ferr_t fwork_retain(fwork_t* work);

/**
 * Releases the given thread.
 *
 * @param thread The work instance to release.
 */
void fwork_release(fwork_t* work);

/**
 * Schedules the given work instance to run on a worker thread sometime in the future.
 *
 * @param  work The work instance to schedule.
 * @param delay Optional delay in nanoseconds to wait before actually scheduling the work.
 *              If this is 0, the work will be scheduled immediately and run as soon as a worker thread is able to run it.
 *              Otherwise, a timer will be created after which the work will be scheduled and run as soon as a worker thread is able to run it.
 *
 * Return values:
 * @retval ferr_ok               The work was successfully scheduled.
 * @retval ferr_invalid_argument One or more of: 1) the work instance was `NULL`, 2) the work instance was already scheduled.
 * @retval ferr_permanent_outage Retaining the work instance failed.
 * @retval ferr_temporary_outage There were insufficient resources to schedule a new work instance.
 */
FERRO_WUR ferr_t fwork_schedule(fwork_t* work, uint64_t delay);

/**
 * Creates and schedules a new work instance to run on a worker thread sometime in the future.
 *
 * @param worker_function The worker function to run.
 * @param            data Optional user-defined data to pass to the worker function.
 * @param           delay Optional delay in nanoseconds to wait before actually scheduling the work. See fwork_schedule() for more information on how this parameter is used.
 * @param[out]   out_work Optional pointer in which a pointer to the newly created work instance will be placed.
 *                        If this is `NULL`, the work instance is managed entirely by the subsystem and the caller will be given no references to the newly created work instance.
 *                        Otherwise, if this is not `NULL`, then the caller is granted a reference on the work instance.
 *
 * @note Passing `NULL` for @p out_work is useful for creating oneshot work instances that you don't need to release later.
 *
 * @retval ferr_ok               The work instance was successfully created and scheduled.
 * @retval ferr_invalid_argument The worker function was `NULL`.
 * @retval ferr_temporary_outage There were insufficient resources to create and schedule a new work instance.
 */
FERRO_WUR ferr_t fwork_schedule_new(fworker_f worker_function, void* data, uint64_t delay, fwork_t** out_work);

/**
 * Cancels the given work instance if it hasn't started running yet.
 *
 * @param id The work instance to cancel.
 *
 * @note This function CANNOT stop a work instance that's already running.
 *
 * Return values:
 * @retval ferr_ok                  The work instance was successfully cancelled.
 * @retval ferr_invalid_argument    The work instance was `NULL`.
 * @retval ferr_already_in_progress The work instance was already running and could not be cancelled. This is also returned if the work has already completed.
 */
FERRO_WUR ferr_t fwork_cancel(fwork_t* work);

/**
 * Waits for the given work instance to complete (or be cancelled).
 *
 * @param work The work instance to wait for.
 *
 * @note If the work has already completed, this function will return immediately.
 *
 * @note If called from a thread context, it will suspend the current thread until the work is done, to save on CPU cycles.
 *       If called from an interrupt context, it will spin-wait until the work is done (which may freeze the system in certain cases).
 *
 * @retval ferr_ok        The work completed successfully.
 * @retval ferr_cancelled The work was cancelled.
 */
ferr_t fwork_wait(fwork_t* work);

/**
 * @}
 */

FERRO_DECLARATIONS_END;

#endif // _FERRO_CORE_WORKERS_H_

