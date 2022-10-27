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

#include <libeve/item.private.h>

void eve_item_set_destructor(eve_item_t* item, eve_item_destructor_f destructor) {
	const eve_item_interface_t* interface = (const void*)sys_object_interface_find(&item->object_class->interface, sys_object_interface_namespace_libeve, eve_object_interface_type_item);
	if (!interface) {
		sys_abort();
	}
	interface->set_destructor(item, destructor);
};

void* eve_item_get_context(eve_item_t* item) {
	const eve_item_interface_t* interface = (const void*)sys_object_interface_find(&item->object_class->interface, sys_object_interface_namespace_libeve, eve_object_interface_type_item);
	if (!interface) {
		sys_abort();
	}
	return interface->get_context(item);
};
