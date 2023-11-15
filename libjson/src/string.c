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

#include <libjson/string.private.h>

static void json_string_destroy(json_object_t* obj) {
	json_string_object_t* string = (void*)obj;

	sys_object_destroy(obj);
};

static const json_object_class_t json_string_class = {
	LIBSYS_OBJECT_CLASS_INTERFACE(NULL),
	.destroy = json_string_destroy,
};

const json_object_class_t* json_object_class_string(void) {
	return &json_string_class;
};

ferr_t json_string_new(const char* contents, json_string_t** out_string) {
	return json_string_new_n(contents, simple_strlen(contents), out_string);
};

ferr_t json_string_new_n(const char* contents, size_t contents_length, json_string_t** out_string) {
	// TODO
	return ferr_unsupported;
};

const char* json_string_contents(json_string_t* string) {
	// TODO
	return NULL;
};

size_t json_string_length(json_string_t* string) {
	// TODO
	return 0;
};
