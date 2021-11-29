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

#ifndef _DYMPLE_UTIL_H_
#define _DYMPLE_UTIL_H_

#include <dymple/base.h>

#include <libsys/libsys.h>

DYMPLE_DECLARATIONS_BEGIN;

#define dymple_abort_status(_expression) ({ \
		ferr_t expression_result = (_expression); \
		if (expression_result != ferr_ok) { \
			sys_console_log_f("Expression returned non-OK status (%s:%d; error = %d, %s, \"%s\"): " #_expression "\n", __FILE__, __LINE__, expression_result, ferr_name(expression_result), ferr_description(expression_result)); \
			sys_abort(); \
		} \
		(void)0; \
	})

DYMPLE_DECLARATIONS_END;

#endif // _DYMPLE_UTIL_H_
