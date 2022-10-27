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

#include <libsimple/vector.h>
#include <libsimple/general.h>

ferr_t simple_vector_init(simple_vector_t* vector, size_t element_count, void* initial_buffer, size_t initial_buffer_size, simple_vector_allocate_f allocate, simple_vector_free_f free, const simple_vector_element_class_t* element_class, void* callback_context) {
	ferr_t status = ferr_ok;

	if (!element_class) {
		status = ferr_invalid_argument;
		goto out;
	}

	if (element_count > initial_buffer_size / element_class->element_size) {
		status = ferr_invalid_argument;
		goto out;
	}

	vector->allocate = allocate;
	vector->free = free;

	simple_memcpy(&vector->element_class, element_class, sizeof(vector->element_class));

	vector->callback_context = callback_context;
	vector->using_static_buffer = true;

	vector->element_count = 0;
	vector->elements = initial_buffer;

out:
	return status;
};

void simple_vector_destroy(simple_vector_t* vector) {

};

size_t simple_vector_push(simple_vector_t* vector, const void* elements, size_t count) {

};

size_t simple_vector_pop(simple_vector_t* vector, void* out_elements, size_t count) {

};

size_t simple_vector_copy(simple_vector_t* vector, size_t old_index, size_t new_index, size_t count, bool allow_expansion) {

};

size_t simple_vector_move(simple_vector_t* vector, size_t old_index, size_t new_index) {

};

size_t simple_vector_copy_out(simple_vector_t* vector, size_t index, void* out_elements, size_t count) {

};

size_t simple_vector_move_out(simple_vector_t* vector, size_t index, void* out_elements, size_t count) {

};

size_t simple_vector_copy_in(simple_vector_t* vector, size_t index, const void* elements, size_t count) {

};

size_t simple_vector_move_in(simple_vector_t* vector, size_t index, void* elements, size_t count) {

};

size_t simple_vector_peek(simple_vector_t* vector, size_t index, void** out_element_pointers, size_t count) {

};

size_t simple_vector_element_count(simple_vector_t* vector) {

};
