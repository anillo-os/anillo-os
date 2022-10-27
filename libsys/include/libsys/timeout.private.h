/*
 * This file is part of Anillo OS
 * Copyright (C) 2022 Anillo OS Developers
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

#ifndef _LIBSYS_TIMEOUT_PRIVATE_H_
#define _LIBSYS_TIMEOUT_PRIVATE_H_

#include <libsys/timeout.h>
#include <gen/libsyscall/syscall-wrappers.h>

LIBSYS_DECLARATIONS_BEGIN;

LIBSYS_ALWAYS_INLINE
libsyscall_timeout_type_t sys_timeout_type_to_libsyscall_timeout_type(sys_timeout_type_t timeout_type) {
	switch (timeout_type) {
		case sys_timeout_type_none:                  return libsyscall_timeout_type_none;
		case sys_timeout_type_absolute_ns_monotonic: return libsyscall_timeout_type_ns_absolute_monotonic;
		case sys_timeout_type_relative_ns_monotonic: return libsyscall_timeout_type_ns_relative;
		default:                                     return libsyscall_timeout_type_none;
	}
};

LIBSYS_DECLARATIONS_END;

#endif // _LIBSYS_TIMEOUT_PRIVATE_H_
