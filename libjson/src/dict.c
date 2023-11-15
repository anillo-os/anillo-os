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

#include <libjson/dict.private.h>

static void json_dict_destroy(json_object_t* obj) {
	json_dict_object_t* dict = (void*)obj;

	sys_object_destroy(obj);
};

static const json_object_class_t json_dict_class = {
	LIBSYS_OBJECT_CLASS_INTERFACE(NULL),
	.destroy = json_dict_destroy,
};

const json_object_class_t* json_object_class_dict(void) {
	return &json_dict_class;
};

ferr_t json_dict_get(json_dict_t* dict, const char* key, sys_object_t** out_value) {
	return json_dict_get_n(dict, key, simple_strlen(key), out_value);
};

ferr_t json_dict_get_n(json_dict_t* obj, const char* key, size_t key_length, sys_object_t** out_value) {
	json_dict_object_t* dict = (void*)obj;

	// TODO
	return ferr_unsupported;
};

size_t json_dict_entries(json_dict_t* dict) {
	// TODO
	return 0;
};

ferr_t json_dict_iterate(json_dict_t* dict, json_dict_iterator_f iterator, void* context) {
	// TODO
	return ferr_unsupported;
};
