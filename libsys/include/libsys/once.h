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

#ifndef _LIBSYS_ONCE_H_
#define _LIBSYS_ONCE_H_

#include <stdint.h>

#include <libsys/base.h>

LIBSYS_DECLARATIONS_BEGIN;

typedef uint64_t sys_once_t;
typedef void (*sys_once_f)(void* context);

#define SYS_ONCE_INITIALIZER 0

void sys_once(sys_once_t* token, sys_once_f initializer, void* context);

LIBSYS_DECLARATIONS_END;

#endif // _LIBSYS_ONCE_H_
