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

#ifndef _LIBSPOOKY_DATA_H_
#define _LIBSPOOKY_DATA_H_

#include <libspooky/base.h>
#include <libspooky/types.h>

LIBSPOOKY_DECLARATIONS_BEGIN;

LIBSPOOKY_OBJECT_CLASS(data);

LIBSPOOKY_WUR ferr_t spooky_data_create(const void* data, size_t length, spooky_data_t** out_data);
LIBSPOOKY_WUR ferr_t spooky_data_create_nocopy(void* data, size_t length, spooky_data_t** out_data);
LIBSPOOKY_WUR ferr_t spooky_data_copy(spooky_data_t* data, spooky_data_t** out_data);
void* spooky_data_contents(spooky_data_t* data);
size_t spooky_data_length(spooky_data_t* data);

spooky_type_t* spooky_type_data(void);

LIBSPOOKY_DECLARATIONS_END;

#endif // _LIBSPOOKY_DATA_H_
