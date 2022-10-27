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

#ifndef _LIBSYS_TIMEOUT_H_
#define _LIBSYS_TIMEOUT_H_

#include <libsys/base.h>
#include <ferro/error.h>

#include <stdint.h>

LIBSYS_DECLARATIONS_BEGIN;

LIBSYS_ENUM(uint8_t, sys_timeout_type) {
	sys_timeout_type_none = 0,
	sys_timeout_type_relative_ns_monotonic = 1,
	sys_timeout_type_absolute_ns_monotonic = 2,
};

LIBSYS_DECLARATIONS_END;

#endif // _LIBSYS_TIMEOUT_H_
