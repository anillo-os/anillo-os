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
 * Timers subsystem.
 */

#ifndef _FERRO_CORE_TIMERS_H_
#define _FERRO_CORE_TIMERS_H_

#include <stdint.h>

#include <ferro/base.h>
#include <ferro/error.h>

FERRO_DECLARATIONS_BEGIN;

/**
 * @addtogroup Timers
 *
 * The timers subsystem.
 *
 * @{
 */

/**
 * Type of a timer callback.
 *
 * @param data User-defined data argument given to one of the timer scheduling functions (e.g. ftimers_oneshot_blocking()).
 */
typedef void (*ftimers_callback_f)(void* data);

/**
 * Type used to identify timers.
 */
typedef uintptr_t ftimers_id_t;

typedef uint64_t ftimers_timestamp_t;

#define FTIMERS_ID_INVALID UINTPTR_MAX

/**
 * Sets up a timer that will only fire once.
 *
 * The callback WILL be called from within an interrupt context and it will NOT be scheduled.
 *
 * Note that this kind of timer should almost never be used, only in special cases (e.g. the scheduler), because it WILL block the CPU until it returns
 * and it will delay other timers waiting to fire.
 *
 * @param delay    Time period to wait before firing the timer, in nanoseconds.
 * @param callback Function to call when the timer fires.
 * @param data     Optional user-defined data argument to be given to the callback when the timer fires.
 * @param out_id   Optional pointer in which the timer ID for the new timer will be placed.
 *
 * @note Timers are not guaranteed to be fired precisely after the given delay.
 *       They are guaranteed to only fire after the given delay, but no guarantee is made about how long it takes for them to be fired after the delay.
 *
 * Return values:
 * @retval ferr_ok               The timer was successfully scheduled.
 * @retval ferr_invalid_argument One or more of: 1) the delay was invalid, 2) the callback was invalid (i.e. `NULL`).
 * @retval ferr_temporary_outage One or more of: 1) no timer backend is currently available to fulfill the request, 2) there were not enough resources to fulfill the request.
 */
FERRO_WUR ferr_t ftimers_oneshot_blocking(uint64_t delay, ftimers_callback_f callback, void* data, ftimers_id_t* out_id);

/**
 * Cancels the timer with the given ID.
 *
 * @param id ID of the timer to cancel.
 *
 * @note If the timer is a oneshot timer and it has already fired or been cancelled, this function will return ::ferr_no_such_resource.
 *
 * Return values:
 * @retval ferr_ok               The timer was successfully cancelled.
 * @retval ferr_no_such_resource No timer with the given ID could be found.
 * @retval ferr_temporary_outage No timer backend is currently available to fulfill the request.
 */
FERRO_WUR ferr_t ftimers_cancel(ftimers_id_t id);

FERRO_WUR ferr_t ftimers_timestamp_read(ftimers_timestamp_t* out_timestamp);

FERRO_WUR ferr_t ftimers_timestamp_delta_to_ns(ftimers_timestamp_t start, ftimers_timestamp_t end, uint64_t* out_ns);

/**
 * @}
 */

FERRO_DECLARATIONS_END;

#endif // _FERRO_CORE_TIMERS_H_
