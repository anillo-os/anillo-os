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

#ifndef _DYMPLE_RESOLUTION_H_
#define _DYMPLE_RESOLUTION_H_

#include <stddef.h>
#include <stdbool.h>

#include <dymple/base.h>
#include <ferro/error.h>

DYMPLE_DECLARATIONS_BEGIN;

DYMPLE_STRUCT_FWD(dymple_image);

DYMPLE_WUR ferr_t dymple_resolve_symbol(dymple_image_t* image, const char* symbol_name, bool search_dependencies, void** out_address);
DYMPLE_WUR ferr_t dymple_resolve_symbol_n(dymple_image_t* image, const char* symbol_name, size_t symbol_name_length, bool search_dependencies, void** out_address);

DYMPLE_DECLARATIONS_END;

#endif // _DYMPLE_RESOLUTION_H_
