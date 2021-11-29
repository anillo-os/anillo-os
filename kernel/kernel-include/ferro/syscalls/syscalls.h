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
 * General syscall-related functions.
 */

#ifndef _FERRO_SYSCALLS_SYSCALLS_H_
#define _FERRO_SYSCALLS_SYSCALLS_H_

#include <ferro/base.h>

FERRO_DECLARATIONS_BEGIN;

/**
 * Initializes the syscall subsystem.
 *
 * This is the function that another kernel subsystem should call.
 * The other `fsyscall_init_*` functions are for internal use.
 */
void fsyscall_init(void);


void fsyscall_init_fd_list_children(void);

FERRO_DECLARATIONS_END;

#endif // _FERRO_SYSCALLS_SYSCALLS_H_
