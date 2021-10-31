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

#include <libsys/general.h>
#include <gen/libsyscall/syscall-wrappers.h>
#include <libsimple/libsimple.h>

ferr_t sys_kernel_log(const char* message) {
	return sys_kernel_log_n(message, simple_strlen(message));
};

ferr_t sys_kernel_log_n(const char* message, size_t message_length) {
	return libsyscall_wrapper_log(message, message_length);
};

void sys_exit(int status) {
	libsyscall_wrapper_exit(status);
	__builtin_unreachable();
};
