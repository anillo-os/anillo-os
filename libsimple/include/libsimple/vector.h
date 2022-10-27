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

#ifndef _LIBSIMPLE_VECTOR_H_
#define _LIBSIMPLE_VECTOR_H_

#include <libsimple/base.h>
#include <ferro/error.h>

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

LIBSIMPLE_DECLARATIONS_BEGIN;

/**
 * A memory allocation callback for vectors.
 *
 * @param context     The callback context given to the vector that is calling this function.
 * @param bytes       The size (in bytes) of how much memory to allocate.
 * @param out_pointer A pointer into which a pointer to the newly allocated memory should be written (upon success).
 *
 * @retval ferr_ok               The allocation succeeded; a pointer to the newly allocated memory has been written into @p out_pointer.
 * @retval ferr_temporary_outage The allocation failed.
 */
typedef ferr_t (*simple_vector_allocate_f)(void* context, size_t bytes, void** out_pointer);

/**
 * A memory release callback for vectors.
 *
 * @param context The callback context given to the vector that is calling this function.
 * @param pointer A pointer to the start of the memory that was allocated.
 * @param bytes   The size of the memory (in bytes) that was allocated.
 */
typedef void (*simple_vector_free_f)(void* context, void* pointer, size_t bytes);

typedef ferr_t (*simple_vector_init_element_f)(void* context, void* element);
typedef ferr_t (*simple_vector_destroy_element_f)(void* context, void* element);
typedef ferr_t (*simple_vector_move_element_f)(void* context, void* old_location, void* new_location);
typedef ferr_t (*simple_vector_copy_element_f)(void* context, const void* old_element, void* new_element);

LIBSIMPLE_STRUCT(simple_vector_element_class) {
	size_t element_size;

	simple_vector_init_element_f init_element;
	simple_vector_destroy_element_f destroy_element;
	simple_vector_move_element_f move_element;
	simple_vector_copy_element_f copy_element;
};

LIBSIMPLE_STRUCT(simple_vector) {
	simple_vector_allocate_f allocate;
	simple_vector_free_f free;
	simple_vector_element_class_t element_class;
	void* callback_context;

	bool using_static_buffer;

	size_t element_count;

	void* elements;
	size_t buffer_size;
};

LIBSIMPLE_WUR ferr_t simple_vector_init(simple_vector_t* vector, size_t initial_element_count, void* initial_buffer, size_t initial_buffer_size, simple_vector_allocate_f allocate, simple_vector_free_f free, const simple_vector_element_class_t* element_class, void* callback_context);
void simple_vector_destroy(simple_vector_t* vector);
LIBSIMPLE_WUR size_t simple_vector_push(simple_vector_t* vector, const void* elements, size_t count);
LIBSIMPLE_WUR size_t simple_vector_pop(simple_vector_t* vector, void* out_elements, size_t count);
LIBSIMPLE_WUR size_t simple_vector_copy(simple_vector_t* vector, size_t old_index, size_t new_index, size_t count, bool allow_expansion);
LIBSIMPLE_WUR size_t simple_vector_move(simple_vector_t* vector, size_t old_index, size_t new_index);
LIBSIMPLE_WUR size_t simple_vector_copy_out(simple_vector_t* vector, size_t index, void* out_elements, size_t count);
LIBSIMPLE_WUR size_t simple_vector_move_out(simple_vector_t* vector, size_t index, void* out_elements, size_t count);
LIBSIMPLE_WUR size_t simple_vector_copy_in(simple_vector_t* vector, size_t index, const void* elements, size_t count);
LIBSIMPLE_WUR size_t simple_vector_move_in(simple_vector_t* vector, size_t index, void* elements, size_t count);
LIBSIMPLE_WUR size_t simple_vector_peek(simple_vector_t* vector, size_t index, void** out_element_pointers, size_t count);
size_t simple_vector_element_count(simple_vector_t* vector);

LIBSIMPLE_DECLARATIONS_END;

#endif // _LIBSIMPLE_VECTOR_H_
