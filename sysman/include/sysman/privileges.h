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

#ifndef _SYSMAN_PRIVILEGES_H_
#define _SYSMAN_PRIVILEGES_H_

#include <sysman/base.h>

SYSMAN_DECLARATIONS_BEGIN;

SYSMAN_STRUCT(sysman_privilege_registry) {
	simple_ghmap_t map;
};

SYSMAN_WUR ferr_t sysman_privilege_registry_init(sysman_privilege_registry_t* registry);

SYSMAN_WUR ferr_t sysman_privilege_registry_get(sysman_privilege_registry_t* registry, const char* name, sys_object_t** out_object);
SYSMAN_WUR ferr_t sysman_privilege_registry_get_n(sysman_privilege_registry_t* registry, const char* name, size_t name_length, sys_object_t** out_object);

SYSMAN_WUR ferr_t sysman_privilege_registry_set(sysman_privilege_registry_t* registry, const char* name, sys_object_t* object);
SYSMAN_WUR ferr_t sysman_privilege_registry_set_n(sysman_privilege_registry_t* registry, const char* name, size_t name_length, sys_object_t* object);

SYSMAN_DECLARATIONS_END;

#endif // _SYSMAN_PRIVILEGES_H_
