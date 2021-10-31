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

#ifndef _LIBSYS_GENERAL_H_
#define _LIBSYS_GENERAL_H_

#include <stdint.h>
#include <stddef.h>

#include <libsys/base.h>
#include <ferro/error.h>

LIBSYS_DECLARATIONS_BEGIN;

/**
 * Logs the given message to the kernel console.
 *
 * @param message The null-terminated message to log.
 *
 * @retval ferr_ok The message was successfully logged.
 * @retval ferr_forbidden The calling process was not permitted to log to the kernel console.
 */
ferr_t sys_kernel_log(const char* message);

/**
 * Exactly like sys_kernel_log(), but the string doesn't have to be null-terminated.
 *
 * @param message        The message to log.
 * @param message_length The length of the message to log.
 *
 * @see sys_kernel_log
 */
ferr_t sys_kernel_log_n(const char* message, size_t message_length);

/**
 * Exits the process immediately.
 *
 * @param status The exit status for this process.
 *
 * @note While @p status can be any caller-desired value, it should be noted that `0` typically indicates a normal, successful exit
 *       while non-zero values typically indicate the process failed at doing whatever it was supposed to do.
 */
LIBSYS_NO_RETURN void sys_exit(int status);

LIBSYS_DECLARATIONS_END;

#endif // _LIBSYS_GENERAL_H_
