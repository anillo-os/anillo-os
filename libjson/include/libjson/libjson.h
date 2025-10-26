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

#ifndef _LIBJSON_LIBJSON_H_
#define _LIBJSON_LIBJSON_H_

#include <libjson/base.h>
#include <libjson/objects.h>

#include <libjson/null.h>
#include <libjson/bool.h>
#include <libjson/string.h>
#include <libjson/number.h>
#include <libjson/dict.h>
#include <libjson/array.h>

#include <libsys/libsys.h>

LIBJSON_DECLARATIONS_BEGIN;

LIBJSON_WUR ferr_t json_parse_string(const char* string, bool json5, json_object_t** out_object);
LIBJSON_WUR ferr_t json_parse_string_n(const char* string, size_t string_length, bool json5, json_object_t** out_object);

LIBJSON_WUR ferr_t json_parse_file(vfs_node_t* file, bool json5, json_object_t** out_object);

LIBJSON_DECLARATIONS_END;

#endif // _LIBJSON_LIBJSON_H_
