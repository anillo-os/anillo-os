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

#include <dymple/log.h>

#ifndef DYMPLE_LOG_DEBUG_ENABLED
	#if defined(NDEBUG) && NDEBUG
		#define DYMPLE_LOG_DEBUG_ENABLED 0
	#else
		#define DYMPLE_LOG_DEBUG_ENABLED 1
	#endif
#endif

static bool dymple_log_debug_enabled = DYMPLE_LOG_DEBUG_ENABLED;

void dymple_log_v(dymple_log_type_t type, dymple_log_category_t category, const char* format, va_list args) {
	if (!dymple_log_is_enabled(type, category)) {
		return;
	}
	sys_console_log_fv(format, args);
};

void dymple_log(dymple_log_type_t type, dymple_log_category_t category, const char* format, ...) {
	va_list args;
	va_start(args, format);
	dymple_log_v(type, category, format, args);
	va_end(args);
};

bool dymple_log_is_enabled(dymple_log_type_t type, dymple_log_category_t category) {
	if (type == dymple_log_type_debug) {
#if DYMPLE_PRINT_LOAD_ADDRESSES
		if (category == dymple_log_category_image_load_address) {
			return true;
		}
#endif
		return dymple_log_debug_enabled;
	}

	return true;
};
