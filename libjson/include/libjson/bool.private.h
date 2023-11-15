/*
 * This file is part of Anillo OS
 * Copyright (C) 2023 Anillo OS Developers
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

#ifndef _LIBJSON_BOOL_PRIVATE_H_
#define _LIBJSON_BOOL_PRIVATE_H_

#include <libjson/bool.h>
#include <libjson/objects.private.h>

LIBJSON_DECLARATIONS_BEGIN;

LIBJSON_STRUCT(json_bool_object) {
	json_object_t object;
};

json_bool_t* json_bool_new(bool value);

bool json_bool_value(json_bool_t* boolean);

LIBJSON_DECLARATIONS_END;

#endif // _LIBJSON_BOOL_PRIVATE_H_