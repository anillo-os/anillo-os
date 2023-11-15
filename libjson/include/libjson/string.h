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

#ifndef _LIBJSON_STRING_H_
#define _LIBJSON_STRING_H_

#include <libjson/base.h>
#include <libjson/objects.h>

LIBJSON_DECLARATIONS_BEGIN;

LIBJSON_OBJECT_CLASS(string);

LIBSYS_WUR ferr_t json_string_new(const char* contents, json_string_t** out_string);
LIBSYS_WUR ferr_t json_string_new_n(const char* contents, size_t contents_length, json_string_t** out_string);

const char* json_string_contents(json_string_t* string);
size_t json_string_length(json_string_t* string);

LIBJSON_DECLARATIONS_END;

#endif // _LIBJSON_STRING_H_
