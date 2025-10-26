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

#ifndef _LIBJSON_ARRAY_H_
#define _LIBJSON_ARRAY_H_

#include <libjson/base.h>
#include <libjson/objects.h>

LIBJSON_DECLARATIONS_BEGIN;

LIBJSON_OBJECT_CLASS(array);

LIBJSON_TYPED_FUNC(bool, json_array_iterator, void* context, size_t index, json_object_t* value);

LIBJSON_WUR ferr_t json_array_new(size_t length, json_object_t* const* values, json_array_t** out_array);

LIBJSON_WUR ferr_t json_array_get(json_array_t* array, size_t index, json_object_t** out_value);
LIBJSON_WUR ferr_t json_array_set(json_array_t* array, size_t index, json_object_t* value);
LIBJSON_WUR ferr_t json_array_append(json_array_t* array, json_object_t* value);
LIBJSON_WUR ferr_t json_array_clear(json_array_t* array, size_t index);

size_t json_array_length(json_array_t* array);

LIBJSON_WUR ferr_t json_array_iterate(json_array_t* array, json_array_iterator_f iterator, void* context);

LIBJSON_DECLARATIONS_END;

#endif // _LIBJSON_ARRAY_H_
