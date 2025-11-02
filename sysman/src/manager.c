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

#define SYSMAN_JSON_KEY_COMMON "common"
#define SYSMAN_JSON_KEY_MANAGER "manager"
#define SYSMAN_JSON_KEY_COMMON_NAME "name"
#define SYSMAN_JSON_KEY_COMMON_WANTS "wants"
#define SYSMAN_JSON_KEY_COMMON_WANTED_BY "wanted_by"
#define SYSMAN_JSON_KEY_COMMON_REQUIRES "requires"
#define SYSMAN_JSON_KEY_COMMON_REQUIRED_BY "required_by"
#define SYSMAN_JSON_KEY_MANAGER_PATH "path"
#define SYSMAN_JSON_KEY_MANAGER_IPC_NAME "ipc_name"
#define SYSMAN_JSON_KEY_MANAGER_PRIVILEGES "privileges"

SYSMAN_STRUCT(sysman_populate_array_context) {
	const char** array;
	void** object_data_start;
};

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

static bool sum_string_space(void* _context, size_t index, json_object_t* value) {
	size_t* string_total_space = _context;

	if (json_object_class(value) != json_object_class_string()) {
		return false;
	}

	*string_total_space += json_string_length(value) + 1;

	return true;
};

static bool populate_string_array(void* _context, size_t index, json_object_t* value) {
	sysman_populate_array_context_t* context = _context;
	size_t length = 0;

	if (json_object_class(value) != json_object_class_string()) {
		return false;
	}

	length = json_string_length(value);

	context->array[index] = *context->object_data_start;
	*context->object_data_start += length + 1;
	simple_memcpy((void*)context->array[index], json_string_contents(value), length);
	((char*)context->array[index])[length] = '\0';

	return true;
};

ferr_t sysman_manager_create_from_json(json_object_t* object, sysman_manager_t** out_manager) {
	ferr_t status = ferr_ok;
	sysman_manager_object_t* manager = NULL;
	size_t string_total_space = 0;
	size_t array_total_entries = 0;
	json_object_t* json_common = NULL;
	json_object_t* json_manager = NULL;
	json_object_t* json_common_name = NULL;
	json_object_t* json_common_wants = NULL;
	json_object_t* json_common_wanted_by = NULL;
	json_object_t* json_common_requires = NULL;
	json_object_t* json_common_required_by = NULL;
	json_object_t* json_manager_path = NULL;
	json_object_t* json_manager_ipc_name = NULL;
	json_object_t* json_manager_privileges = NULL;
	void* object_data_start = NULL;
	sysman_populate_array_context_t populate_array_context = {0};

	if (json_object_class(object) != json_object_class_dict()) {
		status = ferr_invalid_argument;
		goto out;
	}

	if (
		json_dict_get_n(object, SYSMAN_JSON_KEY_COMMON, sizeof(SYSMAN_JSON_KEY_COMMON) - 1, &json_common) != ferr_ok
		|| json_object_class(json_common) != json_object_class_dict()
	) {
		status = ferr_invalid_argument;
		goto out;
	}

	if (
		json_dict_get_n(object, SYSMAN_JSON_KEY_MANAGER, sizeof(SYSMAN_JSON_KEY_MANAGER) - 1, &json_manager) != ferr_ok
		|| json_object_class(json_manager) != json_object_class_dict()
	) {
		status = ferr_invalid_argument;
		goto out;
	}

	if (
		json_dict_get_n(json_common, SYSMAN_JSON_KEY_COMMON_NAME, sizeof(SYSMAN_JSON_KEY_COMMON_NAME) - 1, &json_common_name) != ferr_ok
		|| json_object_class(json_common_name) != json_object_class_string()
	) {
		status = ferr_invalid_argument;
		goto out;
	}

	if (
		json_dict_get_n(json_common, SYSMAN_JSON_KEY_COMMON_WANTS, sizeof(SYSMAN_JSON_KEY_COMMON_WANTS) - 1, &json_common_wants) == ferr_ok
		&& json_object_class(json_common_wants) != json_object_class_array()
	) {
		status = ferr_invalid_argument;
		goto out;
	}

	if (
		json_dict_get_n(json_common, SYSMAN_JSON_KEY_COMMON_WANTED_BY, sizeof(SYSMAN_JSON_KEY_COMMON_WANTED_BY) - 1, &json_common_wanted_by) == ferr_ok
		&& json_object_class(json_common_wanted_by) != json_object_class_array()
	) {
		status = ferr_invalid_argument;
		goto out;
	}

	if (
		json_dict_get_n(json_common, SYSMAN_JSON_KEY_COMMON_REQUIRES, sizeof(SYSMAN_JSON_KEY_COMMON_REQUIRES) - 1, &json_common_requires) == ferr_ok
		&& json_object_class(json_common_requires) != json_object_class_array()
	) {
		status = ferr_invalid_argument;
		goto out;
	}

	if (
		json_dict_get_n(json_common, SYSMAN_JSON_KEY_COMMON_REQUIRED_BY, sizeof(SYSMAN_JSON_KEY_COMMON_REQUIRED_BY) - 1, &json_common_required_by) == ferr_ok
		&& json_object_class(json_common_required_by) != json_object_class_array()
	) {
		status = ferr_invalid_argument;
		goto out;
	}

	if (
		json_dict_get_n(json_manager, SYSMAN_JSON_KEY_MANAGER_PATH, sizeof(SYSMAN_JSON_KEY_MANAGER_PATH) - 1, &json_manager_path) != ferr_ok
		|| json_object_class(json_manager_path) != json_object_class_string()
	) {
		status = ferr_invalid_argument;
		goto out;
	}

	if (
		json_dict_get_n(json_manager, SYSMAN_JSON_KEY_MANAGER_IPC_NAME, sizeof(SYSMAN_JSON_KEY_MANAGER_IPC_NAME) - 1, &json_manager_ipc_name) != ferr_ok
		|| json_object_class(json_manager_ipc_name) != json_object_class_string()
	) {
		status = ferr_invalid_argument;
		goto out;
	}

	if (
		json_dict_get_n(json_manager, SYSMAN_JSON_KEY_MANAGER_PRIVILEGES, sizeof(SYSMAN_JSON_KEY_MANAGER_PRIVILEGES) - 1, &json_manager_privileges) == ferr_ok
		&& json_object_class(json_manager_privileges) != json_object_class_array()
	) {
		status = ferr_invalid_argument;
		goto out;
	}

	string_total_space += json_string_length(json_common_name) + 1;
	string_total_space += json_string_length(json_manager_path) + 1;
	string_total_space += json_string_length(json_manager_ipc_name) + 1;

	if (json_common_wants) {
		if (json_array_iterate(json_common_wants, sum_string_space, &string_total_space) != ferr_ok) {
			status = ferr_invalid_argument;
			goto out;
		}
		array_total_entries += json_array_length(json_common_wants);
	}

	if (json_common_wanted_by) {
		if (json_array_iterate(json_common_wanted_by, sum_string_space, &string_total_space) != ferr_ok) {
			status = ferr_invalid_argument;
			goto out;
		}
		array_total_entries += json_array_length(json_common_wanted_by);
	}

	if (json_common_requires) {
		if (json_array_iterate(json_common_requires, sum_string_space, &string_total_space) != ferr_ok) {
			status = ferr_invalid_argument;
			goto out;
		}
		array_total_entries += json_array_length(json_common_requires);
	}

	if (json_common_required_by) {
		if (json_array_iterate(json_common_required_by, sum_string_space, &string_total_space) != ferr_ok) {
			status = ferr_invalid_argument;
			goto out;
		}
		array_total_entries += json_array_length(json_common_required_by);
	}

	if (json_manager_privileges) {
		if (json_array_iterate(json_manager_privileges, sum_string_space, &string_total_space) != ferr_ok) {
			status = ferr_invalid_argument;
			goto out;
		}
		array_total_entries += json_array_length(json_manager_privileges);
	}

	status = sysman_object_new(&manager_object_class, (sizeof(*manager) - sizeof(manager->object)) + string_total_space + (array_total_entries * sizeof(const char*)), (void*)&manager);
	if (status != ferr_ok) {
		goto out;
	}

	// the data for the arrays and strings starts right after the main object data ends
	object_data_start = (void*)manager + sizeof(*manager);

	if (json_common_wants) {
		manager->wants = object_data_start;
		manager->wants_count = json_array_length(json_common_wants);
		object_data_start += sizeof(*manager->wants) * manager->wants_count;
	}

	if (json_common_wanted_by) {
		manager->wanted_by = object_data_start;
		manager->wanted_by_count = json_array_length(json_common_wanted_by);
		object_data_start += sizeof(*manager->wanted_by) * manager->wanted_by_count;
	}

	if (json_common_requires) {
		manager->requires = object_data_start;
		manager->requires_count = json_array_length(json_common_requires);
		object_data_start += sizeof(*manager->requires) * manager->requires_count;
	}

	if (json_common_required_by) {
		manager->required_by = object_data_start;
		manager->required_by_count = json_array_length(json_common_required_by);
		object_data_start += sizeof(*manager->required_by) * manager->required_by_count;
	}

	if (json_manager_privileges) {
		manager->privileges = object_data_start;
		manager->privileges_count = json_array_length(json_manager_privileges);
		object_data_start += sizeof(*manager->privileges) * manager->privileges_count;
	}

	manager->name = object_data_start;
	manager->name_length = json_string_length(json_common_name);
	object_data_start += manager->name_length + 1;
	simple_memcpy((void*)manager->name, json_string_contents(json_common_name), manager->name_length);
	((char*)manager->name)[manager->name_length] = '\0';

	manager->path = object_data_start;
	manager->path_length = json_string_length(json_manager_path);
	object_data_start += manager->path_length + 1;
	simple_memcpy((void*)manager->path, json_string_contents(json_manager_path), manager->path_length);
	((char*)manager->path)[manager->path_length] = '\0';

	manager->ipc_name = object_data_start;
	manager->ipc_name_length = json_string_length(json_manager_ipc_name);
	object_data_start += manager->ipc_name_length + 1;
	simple_memcpy((void*)manager->ipc_name, json_string_contents(json_manager_ipc_name), manager->ipc_name_length);
	((char*)manager->name)[manager->ipc_name_length] = '\0';

	populate_array_context.object_data_start = &object_data_start;

	populate_array_context.array = manager->wants;
	if (json_common_wants && json_array_iterate(json_common_wants, populate_string_array, &populate_array_context) != ferr_ok) {
		status = ferr_invalid_argument;
		goto out;
	}

	populate_array_context.array = manager->wanted_by;
	if (json_common_wanted_by && json_array_iterate(json_common_wanted_by, populate_string_array, &populate_array_context) != ferr_ok) {
		status = ferr_invalid_argument;
		goto out;
	}

	populate_array_context.array = manager->requires;
	if (json_common_requires && json_array_iterate(json_common_requires, populate_string_array, &populate_array_context) != ferr_ok) {
		status = ferr_invalid_argument;
		goto out;
	}

	populate_array_context.array = manager->required_by;
	if (json_common_required_by && json_array_iterate(json_common_required_by, populate_string_array, &populate_array_context) != ferr_ok) {
		status = ferr_invalid_argument;
		goto out;
	}

	populate_array_context.array = manager->privileges;
	if (json_manager_privileges && json_array_iterate(json_manager_privileges, populate_string_array, &populate_array_context) != ferr_ok) {
		status = ferr_invalid_argument;
		goto out;
	}

out:
	if (status == ferr_ok) {
		*out_manager = (void*)manager;
	} else {
		if (manager) {
			sysman_release((void*)manager);
		}
	}
	return status;
};

const char* sysman_manager_name(sysman_manager_t* obj) {
	sysman_manager_object_t* manager = (void*)obj;
	return manager->name;
};

const char* sysman_manager_ipc_name(sysman_manager_t* obj) {
	sysman_manager_object_t* manager = (void*)obj;
	return manager->ipc_name;
};

ferr_t sysman_manager_start(sysman_manager_t* obj, sysman_privilege_registry_t* privilege_registry) {
	sysman_manager_object_t* manager = (void*)obj;
	ferr_t status = ferr_ok;
	sys_object_t** privileges = NULL;
	size_t initialized_privileges = 0;
	vfs_node_t* file = NULL;

	status = sys_mempool_allocate(sizeof(*privileges) * manager->privileges_count, NULL, (void*)&privileges);
	if (status != ferr_ok) {
		goto out;
	}

	for (size_t i = 0; i < manager->privileges_count; ++i) {
		status = sysman_privilege_registry_get(privilege_registry, manager->privileges[i], &privileges[i]);
		if (status != ferr_ok) {
			goto out;
		}

		++initialized_privileges;
	}

	status = vfs_open_n(manager->path, manager->path_length, &file);
	if (status != ferr_ok) {
		goto out;
	}

	status = sys_proc_create(file, (initialized_privileges > 0) ? privileges : NULL, initialized_privileges, sys_proc_flag_resume, &manager->process);

out:
	if (privileges) {
		if (status != ferr_ok) {
			for (size_t i = 0; i < initialized_privileges; ++i) {
				SYSMAN_WUR_IGNORE(sysman_privilege_registry_set(privilege_registry, manager->privileges[i], privileges[i]));
			}
		}

		SYSMAN_WUR_IGNORE(sys_mempool_free(privileges));
	}
	if (file) {
		sys_release(file);
	}
	return status;
};

sys_proc_id_t sysman_manager_pid(sysman_manager_t* obj) {
	sysman_manager_object_t* manager = (void*)obj;

	if (manager->process) {
		return sys_proc_id(manager->process);
	} else {
		return SYS_PROC_ID_INVALID;
	}
};
