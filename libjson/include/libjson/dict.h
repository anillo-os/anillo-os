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

#ifndef _LIBJSON_DICT_H_
#define _LIBJSON_DICT_H_

#include <libjson/base.h>
#include <libjson/objects.h>

LIBJSON_DECLARATIONS_BEGIN;

LIBJSON_OBJECT_CLASS(dict);

LIBJSON_TYPED_FUNC(bool, json_dict_iterator, void* context, const char* key, size_t key_length, json_object_t* value);

LIBJSON_WUR ferr_t json_dict_new(size_t entries, const char* const* keys, const size_t* key_lengths, json_object_t* const* values, json_dict_t** out_dict);

LIBJSON_WUR ferr_t json_dict_get(json_dict_t* dict, const char* key, json_object_t** out_value);
LIBJSON_WUR ferr_t json_dict_get_n(json_dict_t* dict, const char* key, size_t key_length, json_object_t** out_value);

LIBJSON_WUR ferr_t json_dict_set(json_dict_t* dict, const char* key, json_object_t* value);
LIBJSON_WUR ferr_t json_dict_set_n(json_dict_t* dict, const char* key, size_t key_length, json_object_t* value);

LIBJSON_WUR ferr_t json_dict_clear(json_dict_t* dict, const char* key);
LIBJSON_WUR ferr_t json_dict_clear_n(json_dict_t* dict, const char* key, size_t key_length);

size_t json_dict_entries(json_dict_t* dict);

LIBJSON_WUR ferr_t json_dict_iterate(json_dict_t* dict, json_dict_iterator_f iterator, void* context);

LIBJSON_DECLARATIONS_END;

#endif // _LIBJSON_DICT_H_
