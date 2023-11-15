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

#include <libjson/bool.private.h>

static const json_object_class_t json_bool_class = {
	LIBSYS_OBJECT_CLASS_INTERFACE(NULL),
	.retain = sys_object_retain_noop,
	.release = sys_object_release_noop,
};

const json_object_class_t* json_object_class_bool(void) {
	return &json_bool_class;
};

static json_bool_object_t json_bool_true = {
	.object = {
		.flags = 0,
		.object_class = &json_bool_class,
		.reference_count = 0,
	},
};

static json_bool_object_t json_bool_false = {
	.object = {
		.flags = 0,
		.object_class = &json_bool_class,
		.reference_count = 0,
	},
};

json_bool_t* json_bool_new(bool value) {
	return (json_bool_t*)(value ? &json_bool_true : &json_bool_false);
};

bool json_bool_value(json_bool_t* boolean) {
	return boolean == (json_bool_t*)&json_bool_true;
};
