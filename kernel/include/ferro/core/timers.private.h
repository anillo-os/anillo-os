/**
 * This file is part of Anillo OS
 * Copyright (C) 2020 Anillo OS Developers
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

#ifndef _FERRO_CORE_TIMERS_PRIVATE_H_
#define _FERRO_CORE_TIMERS_PRIVATE_H_

#include <ferro/core/timers.h>

FERRO_DECLARATIONS_BEGIN;

/**
 * Type used to represent a backend-specific timestamp.
 *
 * Whatever value is used for the timestamp must be differentiable (i.e. `timestamp_end - timestamp_start` yields a valid value) and convertible to/from nanoseconds.
 *
 * However, the delta calculation will not be performed directly; two timestamps will be given to the `ftimers_backend_delta_to_ns_f` callback to yield a nanosecond value.
 * Therefore, the backend is free to do whatever it likes with these values; they need not be mathematically/computationally valid.
 * The backend simply needs to be able to produce an accurate value for the elapsed time in nanoseconds between the two timestamps.
 */
typedef uint64_t ftimers_backend_timestamp_t;

/**
 * Backend callback to schedule a call to `ftimers_backend_fire` after the given delay.
 *
 * @param delay Time to wait before making the call, in nanoseconds.
 *
 * @note The `delay` will never be `0`. Therefore, calls to this function must not immediately call `ftimers_backend_fire`.
 *
 * @note A call to this callback MUST replace any previously scheduled/pending call to `ftimers_backend_fire`.
 *
 * @note It IS acceptable for `ftimers_backend_fire` to be called before the given period of time has elapsed.
 *       In this case, the timers subsystem will calculate the new remaining time and re-schedule accordingly.
 *       This is necessary, for example, in cases where the timer backend can only handle a 32-bit counter value.
 */
typedef void (*ftimers_backend_schedule_f)(uint64_t delay);

/**
 * Backend callback to retrieve the current timestamp.
 */
typedef ftimers_backend_timestamp_t (*ftimers_backend_current_timestamp_f)(void);

/**
 * Backend callback to determine how many nanoseconds have elapsed between two timestamps.
 */
typedef uint64_t (*ftimers_backend_delta_to_ns_f)(ftimers_backend_timestamp_t initial, ftimers_backend_timestamp_t final);

/**
 * Cancels any previously scheduled/pending call to `ftimers_backend_fire`.
 */
typedef void (*ftimers_backend_cancel_f)(void);

FERRO_STRUCT(ftimers_backend) {
	const char* name;

	// The smallest delay in nanoseconds that can be resolved correctly.
	// e.g. If the timer can resolve up to 10ns delays but no less than that (lesser delays will be limited to 10ns), then the value for this field would be `10`.
	// Smaller values are better.
	uint32_t precision;

	ftimers_backend_schedule_f schedule;
	ftimers_backend_current_timestamp_f current_timestamp;
	ftimers_backend_delta_to_ns_f delta_to_ns;
	ftimers_backend_cancel_f cancel;
};

/**
 * Registers a new timer backend.
 *
 * @param backend Pointer to an `ftimers_backend_t` structure describing the backend.
 *
 * Return values:
 * @retval ferr_ok               The timer backend was successfully registered
 * @retval ferr_invalid_argument One or more of: 1) the backend was `NULL`, 2) one or more of the required backend functions were `NULL`.
 */
ferr_t ftimers_register_backend(const ftimers_backend_t* backend);

/**
 * Indicates that the first-in-line timer has fired.
 */
void ftimers_backend_fire(void);

FERRO_DECLARATIONS_END;

#endif // _FERRO_CORE_TIMERS_PRIVATE_H_
