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

	if (array->objects) {
		for (size_t i = 0; i < array->length; ++i) {
			json_release(array->objects[i]);
		}

		LIBJSON_WUR_IGNORE(sys_mempool_free(array->objects));
	}

	sys_object_destroy(obj);
};

static const json_object_class_t json_array_class = {
	LIBSYS_OBJECT_CLASS_INTERFACE(NULL),
	.destroy = json_array_destroy,
};

const json_object_class_t* json_object_class_array(void) {
	return &json_array_class;
};

ferr_t json_array_new(size_t length, json_object_t* const* values, json_array_t** out_array) {
	ferr_t status = ferr_ok;
	json_array_object_t* array = NULL;

	status = sys_object_new(&json_array_class, sizeof(*array) - sizeof(array->object), (void*)&array);
	if (status != ferr_ok) {
		goto out;
	}

	array->length = 0;
	array->objects = NULL;

	*out_array = (void*)array;

out:
	return status;
};

ferr_t json_array_get(json_array_t* obj, size_t index, json_object_t** out_value) {
	json_array_object_t* array = (void*)obj;
	ferr_t status = ferr_ok;

	if (index >= array->length) {
		status = ferr_too_big;
		goto out;
	}

	*out_value = array->objects[index];

out:
	return status;
};

ferr_t json_array_set(json_array_t* obj, size_t index, json_object_t* value) {
	json_array_object_t* array = (void*)obj;
	ferr_t status = ferr_ok;

	if (index >= array->length) {
		status = ferr_too_big;
		goto out;
	}

	status = json_retain(value);
	if (status != ferr_ok) {
		goto out;
	}

	json_release(array->objects[index]);
	array->objects[index] = value;

out:
	return status;
};

ferr_t json_array_append(json_array_t* obj, json_object_t* value) {
	json_array_object_t* array = (void*)obj;
	ferr_t status = ferr_ok;

	status = json_retain(value);
	if (status != ferr_ok) {
		value = NULL;
		goto out;
	}

	status = sys_mempool_reallocate(array->objects, sizeof(*array->objects) * (array->length + 1), NULL, (void*)&array->objects);
	if (status != ferr_ok) {
		goto out;
	}

	++array->length;
	array->objects[array->length - 1] = value;

out:
	if (status != ferr_ok) {
		if (value) {
			json_release(value);
		}
	}
	return status;
};

ferr_t json_array_clear(json_array_t* obj, size_t index) {
	json_array_object_t* array = (void*)obj;
	ferr_t status = ferr_ok;

	if (index >= array->length) {
		status = ferr_too_big;
		goto out;
	}

	json_release(array->objects[index]);

	simple_memmove(&array->objects[index], &array->objects[index + 1], (array->length - index) - 1);
	--array->length;

	// just as an optimization; we don't care if it fails
	LIBJSON_WUR_IGNORE(sys_mempool_reallocate(array->objects, sizeof(*array->objects) * array->length, NULL, (void*)&array->objects));

out:
	return status;
};

size_t json_array_length(json_array_t* obj) {
	json_array_object_t* array = (void*)obj;
	return array->length;
};

ferr_t json_array_iterate(json_array_t* obj, json_array_iterator_f iterator, void* context) {
	json_array_object_t* array = (void*)obj;
	ferr_t status = ferr_ok;

	for (size_t i = 0; i < array->length; ++i) {
		if (!iterator(context, i, array->objects[i])) {
			status = ferr_cancelled;
			goto out;
		}
	}

out:
	return status;
};
