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
 * Configuration subsystem.
 */

#ifndef _FERRO_CORE_CONFIG_H_
#define _FERRO_CORE_CONFIG_H_

#include <stddef.h>

#include <ferro/base.h>
#include <ferro/error.h>

FERRO_DECLARATIONS_BEGIN;

/**
 * @addtogroup Configuration
 *
 * The configuration subsystem.
 *
 * @{
 */

/**
 * Initializes the configuration subsystem. Called on kernel startup.
 */
void fconfig_init(const char* data, size_t length);

/**
 * Looks up the configuration entry for the given key and returns a copy of the value, if it is present.
 *
 * @param            key The key to lookup.
 * @param[out] out_value An optional pointer in which a pointer to the copied value string associated with the given key will be written upon success.
 *                       This must be freed with fmempool_free() once the caller is done using it.
 *                       If this is `NULL`, the value is not copied.
 *
 * @note Passing `NULL` for @p out_value is useful for determining whether the given key is present.
 *
 * @retval ferr_ok               The value associated with the given key has been found.
 *                               If @p out_value is non-NULL, the value string has been copied into a new string; a pointer to this string has been written into @p out_value.
 * @retval ferr_no_such_resource The given key was not found within the configuration data.
 * @retval ferr_temporary_outage There were insufficient resources to copy the value string. This only ever returned after the key has been found.
 *                               This will never be returned if @p out_value is `NULL`.
 */
ferr_t fconfig_get(const char* key, char** out_value);

/**
 * Like fconfig_get(), but does not copy the return value.
 *
 * @param                   key The key to lookup.
 * @param[out] out_value_length An optional pointer in which to write the length of the string returned. If the key is not found, this remains unmodified.
 *
 * @returns A reference to the value associated with the given key, or `NULL` if the key was not found.
 *
 * @note Strings returned by this function are not null-terminated! If a non-NULL string is returned, you MUST use
 *       the value returned in @p out_value_length to know how much of the string to use.
 *       If @p out_value_length is `NULL`, you MUST NOT access the string. In that case, the return value is only useful for determining whether the key is present.
 */
const char* fconfig_get_nocopy(const char* key, size_t* out_value_length);

/**
 * @}
 */

FERRO_DECLARATIONS_END;

#endif // _FERRO_CORE_CONFIG_H_
