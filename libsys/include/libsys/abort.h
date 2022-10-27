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

#ifndef _LIBSYS_ABORT_H_
#define _LIBSYS_ABORT_H_

#include <libsys/base.h>
#include <ferro/error.h>

LIBSYS_DECLARATIONS_BEGIN;

LIBSYS_NO_RETURN void sys_abort(void);

#define sys_abort_status(_expression) ({ \
		if ((_expression) != ferr_ok) { \
			sys_abort(); \
		} \
		(void)0; \
	})

#define sys_abort_status_log_helper2(_line) #_line
#define sys_abort_status_log_helper1(_line) sys_abort_status_log_helper2(_line)
#define sys_abort_status_log(_expression) ({ \
		ferr_t status = (_expression); \
		if (status != ferr_ok) { \
			sys_console_log_f(__FILE__ ":" sys_abort_status_log_helper1(__LINE__) ": Expression (\"" #_expression "\") returned non-ok status: %d (%s: %s)", status, ferr_name(status), ferr_description(status)); \
			sys_abort(); \
		} \
		(void)0; \
	})

LIBSYS_DECLARATIONS_END;

#endif // _LIBSYS_ABORT_H_
