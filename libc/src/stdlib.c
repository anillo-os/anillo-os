/*
 * This file is part of Anillo OS
 * Copyright (C) 2021 Anillo OS Developers
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

#include <stdlib.h>
#include <string.h>
#include <libsys/libsys.h>

void exit(int status) {
	sys_exit(status);
};

void* malloc(size_t size) {
	void* mem = NULL;
	if (sys_mempool_allocate(size, NULL, &mem) != ferr_ok) {
		return NULL;
	}
	return mem;
};

void* calloc(size_t element_count, size_t element_size) {
	void* mem = malloc(element_count * element_size);
	if (mem) {
		memset(mem, 0, element_count * element_size);
	}
	return mem;
};

void* realloc(void* old_pointer, size_t new_size) {
	void* mem = NULL;
	if (sys_mempool_reallocate(old_pointer, new_size, NULL, &mem) != ferr_ok) {
		return NULL;
	}
	return mem;
};

void free(void* pointer) {
	sys_abort_status(sys_mempool_free(pointer));
};
