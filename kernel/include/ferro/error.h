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
	ferr_ok                  = 0,

	/**
	 * An unknown error occurred.
	 */
	ferr_unknown             = -1,

	/**
	 * One or more arguments provided were invalid.
	 */
	ferr_invalid_argument    = -2,

	/**
	 * The requested resource is temporarily unavailable.
	 */
	ferr_temporary_outage    = -3,

	/**
	 * The requested resource is permanently unavailable.
	 */
	ferr_permanent_outage    = -4,

	/**
	 * The requested action/service is unsupported.
	 */
	ferr_unsupported         = -5,

	/**
	 * The requested resource could not be found.
	 */
	ferr_no_such_resource    = -6,

	/**
	 * The requested action/service was already in progress.
	 */
	ferr_already_in_progress = -7,

	/**
	 * The operation was cancelled before it could be fully completed.
	 */
	ferr_cancelled           = -8,
};

/**
 * @}
 */

FERRO_DECLARATIONS_END;

#endif // _FERRO_ERROR_H_
