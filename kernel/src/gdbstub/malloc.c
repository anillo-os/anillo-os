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

/**
 * @file
 *
 * A malloc/free stub for GDB.
 *
 * This is necessary for GDB to execute certain JIT'd code.
 */

#include <ferro/core/mempool.h>
#include <ferro/core/console.h>

void* malloc(size_t size) {
	void* result = NULL;
	if (fmempool_allocate(size, NULL, &result) != ferr_ok) {
		result = NULL;
	}
	return result;
};

void* realloc(void* old_address, size_t new_size) {
	void* result = NULL;
	if (fmempool_reallocate(old_address, new_size, NULL, &result) != ferr_ok) {
		result = NULL;
	}
	return result;
};

void free(void* address) {
	if (fmempool_free(address) != ferr_ok) {
		fconsole_log("warning: GDB stub incorrectly used free()");
	}
};
