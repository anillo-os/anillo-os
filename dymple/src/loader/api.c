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

#include <dymple/api.private.h>
#include <libsys/libsys.h>

// TODO: make this a mutex
static sys_mutex_t global_api_lock = SYS_MUTEX_INIT;

void dymple_api_lock(void) {
	sys_mutex_lock(&global_api_lock);
};

void dymple_api_unlock(void) {
	sys_mutex_unlock(&global_api_lock);
};
