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

#include <libjson/null.private.h>

static const json_object_class_t json_null_class = {
	LIBSYS_OBJECT_CLASS_INTERFACE(NULL),
	.retain = sys_object_retain_noop,
	.release = sys_object_release_noop,
};

const json_object_class_t* json_object_class_null(void) {
	return &json_null_class;
};

static json_null_object_t json_null = {
	.object = {
		.flags = 0,
		.object_class = &json_null_class,
		.reference_count = 0,
	},
};

json_null_t* json_null_new(void) {
	return (json_null_t*)&json_null;
};
