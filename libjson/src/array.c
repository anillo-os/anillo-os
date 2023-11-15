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

#include <libjson/array.private.h>

static void json_array_destroy(json_object_t* obj) {
	json_array_object_t* array = (void*)obj;

	sys_object_destroy(obj);
};

static const json_object_class_t json_array_class = {
	LIBSYS_OBJECT_CLASS_INTERFACE(NULL),
	.destroy = json_array_destroy,
};

const json_object_class_t* json_object_class_array(void) {
	return &json_array_class;
};

ferr_t json_array_get(json_array_t* array, size_t index, sys_object_t** out_value) {
	// TODO
	return ferr_unsupported;
};

size_t json_array_length(json_array_t* array) {
	// TODO
	return 0;
};

ferr_t json_array_iterate(json_array_t* array, json_array_iterator_f iterator, void* context) {
	// TODO
	return ferr_unsupported;
};
