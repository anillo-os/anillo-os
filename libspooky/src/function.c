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

#include <libspooky/function.private.h>

static void spooky_function_destroy(spooky_object_t* obj) {
	spooky_function_object_t* function = (void*)obj;

	for (size_t i = 0; i < function->parameter_count; ++i) {
		spooky_release(function->parameters[i].type);
	}
};

static const spooky_object_class_t function_class = {
	LIBSYS_OBJECT_CLASS_INTERFACE(NULL),
	.destroy = spooky_function_destroy,
};

const spooky_object_class_t* spooky_object_class_function(void) {
	return &function_class;
};

ferr_t spooky_function_create(bool wait, const spooky_function_parameter_t* parameters, size_t parameter_count, spooky_function_t** out_function) {
	ferr_t status = ferr_ok;
	size_t retained_params = 0;
	spooky_function_object_t* function = NULL;
	size_t in_offset = 0;
	size_t out_offset = 0;

	for (; retained_params < parameter_count; ++retained_params) {
		if (spooky_retain(parameters[retained_params].type) != ferr_ok) {
			status = ferr_permanent_outage;
			goto out;
		}
	}

	status = sys_object_new(&function_class, (sizeof(*function) - sizeof(function->base.object)) + (sizeof(*function->parameters) * parameter_count), (void*)&function);
	if (status != ferr_ok) {
		goto out;
	}

	// functions cannot be included in structures (at least not for now),
	// so we don't need to specify a byte size (de/serialization doesn't depend on reported byte size)
	function->base.byte_size = 0;

	function->base.global = false;
	function->wait = wait;
	function->parameter_count = parameter_count;

	for (size_t i = 0; i < parameter_count; ++i) {
		spooky_function_parameter_info_t* param_info = &function->parameters[i];
		spooky_type_object_t* param_type;

		param_info->type = parameters[i].type;
		param_info->direction = parameters[i].direction;

		param_type = (void*)param_info->type;

		if (param_info->direction == spooky_function_parameter_direction_in) {
			param_info->offset = in_offset;
			in_offset += param_type->byte_size;
		} else {
			param_info->offset = out_offset;
			out_offset += param_type->byte_size;
		}
	}

out:
	if (status == ferr_ok) {
		*out_function = (void*)function;
	} else if (function) {
		spooky_release((void*)function);
	} else {
		for (size_t i = 0; i < retained_params; ++i) {
			spooky_release(parameters[i].type);
		}
	}
	return status;
};
