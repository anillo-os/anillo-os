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

#ifndef _LIBSYS_GHMAP_H_
#define _LIBSYS_GHMAP_H_

#include <libsys/base.h>
#include <ferro/error.h>

#include <libsimple/libsimple.h>

LIBSYS_DECLARATIONS_BEGIN;

/**
 * An implementation of ::simple_ghmap_allocate_f using libsys's memory pool.
 */
ferr_t simple_ghmap_allocate_sys_mempool(void* context, size_t bytes, void** out_pointer);

/**
 * An implementation of ::simple_ghmap_free_f using libsys's memory pool.
 */
void simple_ghmap_free_sys_mempool(void* context, void* pointer, size_t bytes);

LIBSYS_DECLARATIONS_END;

#endif // _LIBSYS_GHMAP_H_
