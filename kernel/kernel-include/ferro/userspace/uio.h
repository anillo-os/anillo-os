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

#ifndef _FERRO_USERSPACE_UIO_H_
#define _FERRO_USERSPACE_UIO_H_

#include <stdint.h>
#include <stddef.h>

#include <ferro/base.h>
#include <ferro/error.h>

FERRO_DECLARATIONS_BEGIN;

FERRO_WUR ferr_t ferro_uio_copy_in(uintptr_t user_address, size_t size, void** out_copy);
FERRO_WUR ferr_t ferro_uio_copy_in_noalloc(uintptr_t user_address, size_t size, void* out_buffer);
FERRO_WUR ferr_t ferro_uio_copy_out(const void* buffer, size_t size, uintptr_t user_address);

void ferro_uio_copy_free(void* copy, size_t size);

FERRO_WUR ferr_t ferro_uio_atomic_load_1_relaxed(uintptr_t user_address, uint8_t* out_value);
FERRO_WUR ferr_t ferro_uio_atomic_load_8_relaxed(uintptr_t user_address, uint64_t* out_value);

FERRO_WUR ferr_t ferro_uio_atomic_store_8_relaxed(uintptr_t user_address, uint64_t value);

FERRO_DECLARATIONS_END;

#endif // _FERRO_USERSPACE_UIO_H_
