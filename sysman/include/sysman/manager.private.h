/*
 * This file is part of Anillo OS
 * Copyright (C) 2025 Anillo OS Developers
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

#ifndef _SYSMAN_MANAGER_PRIVATE_H_
#define _SYSMAN_MANAGER_PRIVATE_H_

#include <sysman/manager.h>
#include <sysman/objects.private.h>

SYSMAN_DECLARATIONS_BEGIN;

SYSMAN_STRUCT(sysman_manager_object) {
	sysman_object_t object;
	const char* name;
	size_t name_length;
	const char** wants;
	size_t wants_count;
	const char** wanted_by;
	size_t wanted_by_count;
	const char** requires;
	size_t requires_count;
	const char** required_by;
	size_t required_by_count;
	const char* path;
	size_t path_length;
	const char* ipc_name;
	size_t ipc_name_length;
	const char** privileges;
	size_t privileges_count;

	sys_proc_t* process;
};

SYSMAN_DECLARATIONS_END;

#endif // _SYSMAN_MANAGER_PRIVATE_H_
