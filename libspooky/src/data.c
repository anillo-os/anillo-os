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

#include <libspooky/data.private.h>
#include <libspooky/types.private.h>

static void spooky_data_destroy(spooky_object_t* obj) {
	spooky_data_object_t* data = (void*)obj;

	if (data->owns_contents) {
		LIBSPOOKY_WUR_IGNORE(sys_mempool_free(data->contents));
	}
};

static const spooky_object_class_t data_class = {
	LIBSYS_OBJECT_CLASS_INTERFACE(NULL),
	.destroy = spooky_data_destroy,
};

static spooky_type_object_t data_type = {
	.object = {
		.object_class = &spooky_direct_object_class_type,
		.reference_count = 0,
		.flags = 0,
	},
	.byte_size = sizeof(spooky_data_t*),
	.global = true,
};

const spooky_object_class_t* spooky_object_class_data(void) {
	return &data_class;
};

ferr_t spooky_data_create(const void* contents, size_t length, spooky_data_t** out_data) {
	ferr_t status = ferr_ok;
	spooky_data_object_t* data = NULL;
	void* contents_ptr = NULL;

	status = sys_mempool_allocate(length, NULL, &contents_ptr);
	if (status != ferr_ok) {
		goto out;
	}

	status = sys_object_new(&data_class, sizeof(*data) - sizeof(data->object), (void*)&data);
	if (status != ferr_ok) {
		goto out;
	}

	data->length = length;
	data->contents = contents_ptr;
	data->owns_contents = true;
	if (contents) {
		simple_memcpy(data->contents, contents, length);
	}

out:
	if (status == ferr_ok) {
		*out_data = (void*)data;
	} else if (data) {
		spooky_release((void*)data);
	} else {
		if (contents_ptr) {
			LIBSPOOKY_WUR_IGNORE(sys_mempool_free(contents_ptr));
		}
	}
	return status;
};

ferr_t spooky_data_create_nocopy(void* contents, size_t length, spooky_data_t** out_data) {
	ferr_t status = ferr_ok;
	spooky_data_object_t* data = NULL;

	status = sys_object_new(&data_class, sizeof(*data) - sizeof(data->object), (void*)&data);
	if (status != ferr_ok) {
		goto out;
	}

	data->length = length;
	data->contents = contents;
	data->owns_contents = false;

out:
	if (status == ferr_ok) {
		*out_data = (void*)data;
	} else if (data) {
		spooky_release((void*)data);
	}
	return status;
};

ferr_t spooky_data_create_transfer(void* contents, size_t length, spooky_data_t** out_data) {
	ferr_t status = ferr_ok;
	spooky_data_object_t* data = NULL;

	status = sys_object_new(&data_class, sizeof(*data) - sizeof(data->object), (void*)&data);
	if (status != ferr_ok) {
		goto out;
	}

	data->length = length;
	data->contents = contents;
	data->owns_contents = true;

out:
	if (status == ferr_ok) {
		*out_data = (void*)data;
	} else if (data) {
		spooky_release((void*)data);
	}
	return status;
};

ferr_t spooky_data_copy(spooky_data_t* obj, spooky_data_t** out_data) {
	spooky_data_object_t* other = (void*)obj;
	return spooky_data_create(other->contents, other->length, out_data);
};

void* spooky_data_contents(spooky_data_t* obj) {
	spooky_data_object_t* data = (void*)obj;
	return data->contents;
};

size_t spooky_data_length(spooky_data_t* obj) {
	spooky_data_object_t* data = (void*)obj;
	return data->length;
};

spooky_type_t* spooky_type_data(void) {
	return (void*)&data_type;
};
