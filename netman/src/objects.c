/*
 * This file is part of Anillo OS
 * Copyright (C) 2022 Anillo OS Developers
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

#include <netman/objects.private.h>

ferr_t netman_retain(netman_object_t* object) {
	return sys_retain(object);
};

void netman_release(netman_object_t* object) {
	return sys_release(object);
};

const netman_object_class_t* netman_object_class(netman_object_t* object) {
	return sys_object_class(object);
};

ferr_t netman_object_new(const netman_object_class_t* object_class, size_t extra_bytes, netman_object_t** out_object) {
	ferr_t status = sys_object_new(object_class, extra_bytes, out_object);
	if (status == ferr_ok) {
		simple_memset(*out_object + 1, 0, extra_bytes);
	}
	return status;
};
