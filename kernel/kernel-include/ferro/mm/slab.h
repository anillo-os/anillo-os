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

#ifndef _FERRO_MM_SLAB_H_
#define _FERRO_MM_SLAB_H_

#include <stddef.h>

#include <ferro/base.h>
#include <ferro/error.h>
#include <ferro/core/paging.h>

FERRO_DECLARATIONS_BEGIN;

FERRO_STRUCT(fslab_element) {
	fslab_element_t* next;
};

FERRO_STRUCT(fslab_region) {
	fslab_region_t* next;
	fslab_element_t* elements;
};

FERRO_STRUCT(fslab) {
	flock_spin_intsafe_t lock;
	fslab_region_t* regions;
	size_t element_size;
	size_t element_alignment;
};

#define FSLAB_INITIALIZER(_size, _align) (fslab_t){FLOCK_SPIN_INTSAFE_INIT, NULL, (size_t)(_size), (size_t)(_align)}

#define FSLAB_INITIALIZER_TYPE(type) FSLAB_INITIALIZER(sizeof(type), _Alignof(type))

FERRO_ALWAYS_INLINE void fslab_init(fslab_t* slab, size_t element_size, size_t element_alignment) {
	*slab = FSLAB_INITIALIZER(element_size, element_alignment);
};

void fslab_destroy(fslab_t* slab);

FERRO_WUR ferr_t fslab_allocate(fslab_t* slab, void** out_element);
FERRO_WUR ferr_t fslab_free(fslab_t* slab, void* element);

FERRO_DECLARATIONS_END;

#endif // _FERRO_MM_SLAB_H_
