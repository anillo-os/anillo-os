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

#include <ferro/core/ghmap.h>
#include <ferro/core/mempool.h>
#include <ferro/core/panic.h>

ferr_t __attribute__((optnone)) simple_ghmap_allocate_mempool(void* context, size_t bytes, void** out_pointer) {
	return (fmempool_allocate(bytes, NULL, out_pointer) == ferr_ok ? ferr_ok : ferr_temporary_outage);
};

void __attribute__((optnone)) simple_ghmap_free_mempool(void* context, void* pointer, size_t bytes) {
	fpanic_status(fmempool_free(pointer));
};
