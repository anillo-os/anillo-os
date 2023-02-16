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
 * List of error codes that Ferro functions can return.
 */

#ifndef _FERRO_ERROR_H_
#define _FERRO_ERROR_H_

#include <ferro/base.h>

FERRO_DECLARATIONS_BEGIN;

/**
 * @addtogroup Error
 *
 * Error codes used throughout the kernel.
 *
 * @{
 */

FERRO_ENUM(int, ferr) {
	/**
	 * No error; success.
	 */
	ferr_ok                   = 0,

	/**
	 * An unknown error occurred.
	 */
	ferr_unknown              = -1,

	/**
	 * One or more arguments provided were invalid.
	 */
	ferr_invalid_argument     = -2,

	/**
	 * The requested resource is temporarily unavailable.
	 */
	ferr_temporary_outage     = -3,

	/**
	 * The requested resource is permanently unavailable.
	 */
	ferr_permanent_outage     = -4,

	/**
	 * The requested action/service is unsupported.
	 */
	ferr_unsupported          = -5,

	/**
	 * The requested resource could not be found.
	 */
	ferr_no_such_resource     = -6,

	/**
	 * The requested action/service was already in progress.
	 */
	ferr_already_in_progress  = -7,

	/**
	 * The operation was cancelled before it could be fully completed.
	 */
	ferr_cancelled            = -8,

	/**
	 * One or more of: 1) one of the input operands was too large to be processed, or 2) the result/output was too large to return.
	 */
	ferr_too_big              = -9,

	/**
	 * Some data (whether input, output, or internal) failed a checksum.
	 */
	ferr_invalid_checksum     = -10,

	/**
	 * The requested action/service/operation was not completed and should be restarted.
	 */
	ferr_should_restart       = -11,

	/**
	 * The caller was not allowed to access the requested action/service/operation/resource.
	 */
	ferr_forbidden            = -12,

	/**
	 * One or more of: 1) one of the input operands was too small to be processed, or 2) the result/output was too small to return.
	 */
	ferr_too_small            = -13,

	/**
	 * The requested resource was unavailable.
	 *
	 * This is a sort of middleground between ::ferr_temporary_outage and ::ferr_permanent_outage.
	 * The resource isn't permanently unavailable, but it's not likely to become available any time soon.
	 */
	ferr_resource_unavailable = -14,

	/**
	 * Completing the requested action/service/operation would require waiting but doing so has been disallowed.
	 */
	ferr_no_wait              = -15,

	/**
	 * A timeout was set for the given action/service/operation and it expired before the action/service/operation could be completed.
	 */
	ferr_timed_out            = -16,

	/**
	 * A signal arrived before or during the operation and it was not completed.
	 */
	ferr_signaled             = -17,

	/**
	 * The operation was aborted and should not be tried again.
	 */
	ferr_aborted              = -18,
};

static const char* ferr_names[] = {
	"ferr_ok",
	"ferr_unknown",
	"ferr_invalid_argument",
	"ferr_temporary_outage",
	"ferr_permanent_outage",
	"ferr_unsupported",
	"ferr_no_such_resource",
	"ferr_already_in_progress",
	"ferr_cancelled",
	"ferr_too_big",
	"ferr_invalid_checksum",
	"ferr_should_restart",
	"ferr_forbidden",
	"ferr_too_small",
	"ferr_resource_unavailable",
	"ferr_no_wait",
	"ferr_timed_out",
	"ferr_signaled",
	"ferr_aborted",
};

static const char* ferr_descriptions[] = {
	"No error; success.",
	"An unknown error occurred.",
	"One or more arguments provided were invalid.",
	"The requested resource is temporarily unavailable.",
	"The requested resource is permanently unavailable.",
	"The requested action/service is unsupported.",
	"The requested resource could not be found.",
	"The requested action/service was already in progress.",
	"The operation was cancelled before it could be fully completed.",
	"One or more of: 1) one of the input operands was too large to be processed, or 2) the result/output was too large to return.",
	"Some data (whether input, output, or internal) failed a checksum.",
	"The requested action/service/operation was not completed and should be restarted.",
	"The caller was not allowed to access the requested action/service/operation/resource.",
	"One or more of: 1) one of the input operands was too small to be processed, or 2) the result/output was too small to return.",
	"The requested resource was unavailable.",
	"Completing the requested action/service/operation would require waiting but doing so has been disallowed.",
	"A timeout was set for the given action/service/operation and it expired before the action/service/operation could be completed.",
	"A signal arrived before or during the operation and it was not completed.",
	"The operation was aborted and should not be tried again.",
};

FERRO_ALWAYS_INLINE const char* ferr_name(ferr_t error) {
	if (error < 0) {
		error *= -1;
	}

	if (error >= sizeof(ferr_names)) {
		return "ferr_xxx_invalid_error";
	}

	return ferr_names[error];
};

FERRO_ALWAYS_INLINE const char* ferr_description(ferr_t error) {
	if (error < 0) {
		error *= -1;
	}

	if (error >= sizeof(ferr_descriptions)) {
		return "This is an invalid/unknown error code.";
	}

	return ferr_descriptions[error];
};

/**
 * @}
 */

FERRO_DECLARATIONS_END;

#endif // _FERRO_ERROR_H_
