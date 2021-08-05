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

#ifndef _FERRO_CORE_CONSOLE_H_
#define _FERRO_CORE_CONSOLE_H_

#include <stddef.h>
#include <stdarg.h>

#include <ferro/base.h>
#include <ferro/core/framebuffer.h>

FERRO_DECLARATIONS_BEGIN;

#define FERRO_PRINTF(a, b) __attribute__((format(printf, a, b)))

/**
 * Initializes the console subsystem. Called on kernel startup.
 */
void fconsole_init(void);

/**
 * Logs a UTF-8 string to the console (without formatting).
 *
 * @param string The UTF-8 string to log.
 *
 * Return values:
 * @retval ferr_ok                Successfully logged the given string.
 * @retval ferr_invalid_argument  The given string was not a valid UTF-8 string.
 *
 * @note This method automatically determines the length of the string by counting the number of bytes before the first null terminator.
 *       As such, UTF-8 string containing null terminators cannot be logged with this function. Instead, use `fconsole_logn`.
 */
ferr_t fconsole_log(const char* string);

/**
 * Logs a UTF-8 character array to the console (without formatting).
 *
 * @param string The UTF-8 character array to log.
 * @param size   The number of characters to print from the character array.
 *
 * Return values:
 * @retval ferr_ok                Successfully logged the given character array.
 * @retval ferr_invalid_argument  The given character array contained one or more invalid UTF-8 sequences.
 */
ferr_t fconsole_logn(const char* string, size_t size);

/**
 * Logs a UTF-8 string to the console (with printf-style formatting).
 *
 * @param string The UTF-8 string to log.
 *
 * Return values:
 * @retval ferr_ok                Successfully logged the given string.
 * @retval ferr_invalid_argument  One or more of: 1) the given string was not a valid UTF-8 string, 2) one of the format arguments was invalid.
 *
 * @note See the note on `fconsole_log`.
 *
 * @note Strings and character arrays passed as format arguments are also interpretted as UTF-8 strings and character arrays.
 */
FERRO_PRINTF(1, 2)
ferr_t fconsole_logf(const char* format, ...);

/**
 * See `fconsole_logf`. This function is almost identical, except that it accepts its format arguments in a `va_list` rather than directly.
 */
FERRO_PRINTF(1, 0)
ferr_t fconsole_logfv(const char* format, va_list args);

/**
 * Logs a UTF-8 character array to the console (with printf-style formatting).
 *
 * @param string The UTF-8 character array to log.
 * @param size   The number of characters to print from the character array.
 *
 * Return values:
 * @retval ferr_ok                Successfully logged the given character array.
 * @retval ferr_invalid_argument  One or more of: 1) the given character array contained one or more invalid UTF-8 sequences, 2) one of the format arguments was invalid.
 *
 * @note Strings and character arrays passed as format arguments are also interpretted as UTF-8 strings and character arrays.
 */
FERRO_PRINTF(1, 3)
ferr_t fconsole_lognf(const char* format, size_t format_size, ...);

/**
 * See `fconsole_lognf`. This function is almost identical, except that it accepts its format arguments in a `va_list` rather than directly.
 */
FERRO_PRINTF(1, 0)
ferr_t fconsole_lognfv(const char* format, size_t format_size, va_list args);

FERRO_DECLARATIONS_END;

#endif // _FERRO_CORE_CONSOLE_H_
