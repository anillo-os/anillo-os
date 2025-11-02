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

#include <sysman/privileges.h>

ferr_t sysman_privilege_registry_init(sysman_privilege_registry_t* registry) {
	ferr_t status = ferr_ok;

	status = simple_ghmap_init_string_to_generic(&registry->map, 64, sizeof(sys_object_t*), simple_ghmap_allocate_sys_mempool, simple_ghmap_free_sys_mempool, NULL);
	if (status != ferr_ok) {
		goto out;
	}

out:
	return status;
};

ferr_t sysman_privilege_registry_get(sysman_privilege_registry_t* registry, const char* name, sys_object_t** out_object) {
	return sysman_privilege_registry_get_n(registry, name, simple_strlen(name), out_object);
};

ferr_t sysman_privilege_registry_get_n(sysman_privilege_registry_t* registry, const char* name, size_t name_length, sys_object_t** out_object) {
	ferr_t status = ferr_ok;
	sys_object_t** object_ptr = NULL;

	status = simple_ghmap_lookup(&registry->map, name, name_length, false, SIZE_MAX, NULL, (void*)&object_ptr, NULL);
	if (status != ferr_ok) {
		goto out;
	}

	if (out_object) {
		// this CANNOT fail
		sys_abort_status_log(simple_ghmap_clear(&registry->map, name, name_length));

		*out_object = *object_ptr;
	}

out:
	return status;
};

ferr_t sysman_privilege_registry_set(sysman_privilege_registry_t* registry, const char* name, sys_object_t* object) {
	return sysman_privilege_registry_set_n(registry, name, simple_strlen(name), object);
};

ferr_t sysman_privilege_registry_set_n(sysman_privilege_registry_t* registry, const char* name, size_t name_length, sys_object_t* object) {
	ferr_t status = ferr_ok;
	sys_object_t** object_ptr = NULL;
	bool created = false;

	status = simple_ghmap_lookup(&registry->map, name, name_length, true, SIZE_MAX, &created, (void*)&object_ptr, NULL);
	if (status != ferr_ok) {
		goto out;
	}

	if (!created) {
		status = ferr_already_in_progress;
		goto out;
	}

	*object_ptr = object;

out:
	return status;
};
