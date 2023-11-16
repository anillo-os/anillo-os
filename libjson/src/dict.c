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

LIBJSON_STRUCT(json_dict_iterator_helper_context) {
	json_dict_iterator_f iterator;
	void* context;
};

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

ferr_t json_dict_new(size_t entries, const char* const* keys, const size_t* key_lengths, json_object_t* const* values, json_dict_t** out_dict) {
	ferr_t status = ferr_ok;
	json_dict_object_t* dict = NULL;

	status = sys_object_new(&json_dict_class, sizeof(*dict) - sizeof(dict->object), (void*)&dict);
	if (status != ferr_ok) {
		goto out;
	}

	status = simple_ghmap_init_string_to_generic(&dict->map, 0, sizeof(json_object_t*), simple_ghmap_allocate_sys_mempool, simple_ghmap_free_sys_mempool, NULL);
	if (status != ferr_ok) {
		goto out;
	}

	*out_dict = (void*)dict;

out:
	if (status != ferr_ok) {
		if (dict) {
			// do NOT use `json_release` because our destructor assumes the hashmap has been initialized
			sys_object_destroy((void*)dict);
		}
	}
	return status;
};

ferr_t json_dict_get(json_dict_t* dict, const char* key, json_object_t** out_value) {
	return json_dict_get_n(dict, key, simple_strlen(key), out_value);
};

ferr_t json_dict_get_n(json_dict_t* obj, const char* key, size_t key_length, json_object_t** out_value) {
	json_dict_object_t* dict = (void*)obj;
	ferr_t status = ferr_ok;
	json_object_t** ptr = NULL;

	status = simple_ghmap_lookup(&dict->map, key, key_length, false, 0, NULL, (void*)&ptr, NULL);
	if (status != ferr_ok) {
		goto out;
	}

	*out_value = *ptr;

out:
	return status;
};

ferr_t json_dict_set(json_dict_t* dict, const char* key, json_object_t* value) {
	return json_dict_set_n(dict, key, simple_strlen(key), value);
};

ferr_t json_dict_set_n(json_dict_t* obj, const char* key, size_t key_length, json_object_t* value) {
	json_dict_object_t* dict = (void*)obj;
	ferr_t status = ferr_ok;
	bool created = false;
	json_object_t** ptr = NULL;

	status = json_retain(value);
	if (status != ferr_ok) {
		value = NULL;
		goto out;
	}

	status = simple_ghmap_lookup(&dict->map, key, key_length, true, SIZE_MAX, &created, (void*)&ptr, NULL);
	if (status != ferr_ok) {
		goto out;
	}

	if (!created) {
		json_release(*ptr);
	}

	*ptr = value;

out:
	if (status != ferr_ok) {
		if (value) {
			json_release(value);
		}
	}
	return status;
};

ferr_t json_dict_clear(json_dict_t* dict, const char* key) {
	return json_dict_clear_n(dict, key, simple_strlen(key));
};

ferr_t json_dict_clear_n(json_dict_t* obj, const char* key, size_t key_length) {
	json_dict_object_t* dict = (void*)obj;
	ferr_t status = ferr_ok;
	json_object_t** ptr = NULL;

	status = simple_ghmap_lookup(&dict->map, key, key_length, false, 0, NULL, (void*)&ptr, NULL);
	if (status != ferr_ok) {
		goto out;
	}

	json_release(*ptr);

	// this CANNOT fail
	sys_abort_status_log(simple_ghmap_clear(&dict->map, key, key_length));

out:
	return status;
};

size_t json_dict_entries(json_dict_t* obj) {
	json_dict_object_t* dict = (void*)obj;
	return simple_ghmap_entry_count(&dict->map);
};

static bool json_dict_iterator_helper(void* ctxt, simple_ghmap_t* hashmap, simple_ghmap_hash_t hash, const void* key, size_t key_size, void* entry, size_t entry_size) {
	json_dict_iterator_helper_context_t* context = ctxt;
	return context->iterator(context->context, key, key_size, *(json_object_t**)entry);
};

ferr_t json_dict_iterate(json_dict_t* obj, json_dict_iterator_f iterator, void* context) {
	json_dict_object_t* dict = (void*)obj;
	json_dict_iterator_helper_context_t iterator_context = {
		.iterator = iterator,
		.context = context,
	};
	return simple_ghmap_for_each(&dict->map, json_dict_iterator_helper, &iterator_context);
};
