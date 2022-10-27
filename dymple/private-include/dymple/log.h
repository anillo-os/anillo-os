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

#ifndef _DYMPLE_LOG_H_
#define _DYMPLE_LOG_H_

#include <dymple/base.h>

#include <libsys/libsys.h>

DYMPLE_DECLARATIONS_BEGIN;

DYMPLE_ENUM(uint64_t, dymple_log_category) {
	dymple_log_category_general       = 0,
	dymple_log_category_image_loading = 1,
	dymple_log_category_relocations = 2,
	dymple_log_category_resolution = 3,
	dymple_log_category_image_load_address = 4,
};

DYMPLE_ENUM(uint8_t, dymple_log_type) {
	dymple_log_type_debug   = 0,
	dymple_log_type_info    = 1,
	dymple_log_type_warning = 2,
	dymple_log_type_error   = 3,
};

DYMPLE_PRINTF(3, 0)
void dymple_log_v(dymple_log_type_t type, dymple_log_category_t category, const char* format, va_list args);

DYMPLE_PRINTF(3, 4)
void dymple_log(dymple_log_type_t type, dymple_log_category_t category, const char* format, ...);

bool dymple_log_is_enabled(dymple_log_type_t type, dymple_log_category_t category);

#define   dymple_log_debug(category, format, ...) dymple_log(dymple_log_type_debug,   category, format, ##__VA_ARGS__)
#define    dymple_log_info(category, format, ...) dymple_log(dymple_log_type_info,    category, format, ##__VA_ARGS__)
#define dymple_log_warning(category, format, ...) dymple_log(dymple_log_type_warning, category, format, ##__VA_ARGS__)
#define   dymple_log_error(category, format, ...) dymple_log(dymple_log_type_error,   category, format, ##__VA_ARGS__)

DYMPLE_DECLARATIONS_END;

#endif // _DYMPLE_LOG_H_
