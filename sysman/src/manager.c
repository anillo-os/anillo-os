/*
 * This file is part of Anillo OS
 * Copyright (C) 2025 Anillo OS Developers
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

#include <sysman/manager.private.h>

static void sysman_manager_destroy(sys_object_t* object);

static const sysman_object_class_t manager_object_class = {
	LIBSYS_OBJECT_CLASS_INTERFACE(NULL),
	.destroy = sysman_manager_destroy,
};

static void sysman_manager_destroy(sys_object_t* obj) {
	sysman_manager_object_t* object = (void*)obj;

	sys_object_destroy(obj);
};

const sysman_object_class_t* sysman_object_class_manager() {
	return &manager_object_class;
};
