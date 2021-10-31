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
 * Panic subsystem.
 */

#ifndef _FERRO_CORE_PANIC_H_
#define _FERRO_CORE_PANIC_H_

#include <stdarg.h>

#include <ferro/base.h>
#include <ferro/error.h>

FERRO_DECLARATIONS_BEGIN;

/**
 * @addtogroup Panic
 *
 * The panic subsystem.
 *
 * @{
 */

/**
 * Sentences the kernel (and the entire system) to death, basically.
 *
 * This function will never return to its caller. It starts a chain of events that result in the kernel giving up control of the system.
 *
 * @param reason_format Optional reason format string to explain why the system is dying.
 */
FERRO_PRINTF(1, 2)
FERRO_NO_RETURN void fpanic(const char* reason_format, ...);

/**
 * @see fpanic
 */
FERRO_PRINTF(1, 0)
FERRO_NO_RETURN void fpanicv(const char* reason_format, va_list args);

/**
 * A useful macro to automatically panic when the result of an expression is not ::ferr_ok.
 */
#define fpanic_status(expr) ({ \
		ferr_t status = (expr); \
		if (status != ferr_ok) { \
			fpanic("Expression returned non-OK status %d; %s @ %s:%d", status, #expr, __FILE__, __LINE__); \
		} \
	})

/**
 * @}
 */

#endif // _FERRO_CORE_PANIC_H_
