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

#ifndef _LIBSYS_PAGES_H_
#define _LIBSYS_PAGES_H_

#include <stddef.h>
#include <stdint.h>

#include <libsys/base.h>
#include <ferro/error.h>

LIBSYS_DECLARATIONS_BEGIN;

LIBSYS_WUR ferr_t sys_page_allocate(size_t page_count, uint64_t flags, void** out_address);

LIBSYS_WUR ferr_t sys_page_free(void* address);

LIBSYS_DECLARATIONS_END;

#endif // _LIBSYS_PAGES_H_
