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

#ifndef _DYMPLE_IMAGES_H_
#define _DYMPLE_IMAGES_H_

#include <dymple/base.h>

#include <libsys/libsys.h>
#include <ferro/error.h>

DYMPLE_DECLARATIONS_BEGIN;

DYMPLE_STRUCT_FWD(dymple_image);

DYMPLE_WUR ferr_t dymple_load_image_by_name(const char* name, dymple_image_t** out_image);
DYMPLE_WUR ferr_t dymple_load_image_by_name_n(const char* name, size_t name_length, dymple_image_t** out_image);

DYMPLE_WUR ferr_t dymple_load_image_from_file(sys_file_t* file, dymple_image_t** out_image);

DYMPLE_WUR ferr_t dymple_find_loaded_image_by_name(const char* name, dymple_image_t** out_image);
DYMPLE_WUR ferr_t dymple_find_loaded_image_by_name_n(const char* name, size_t name_length, dymple_image_t** out_image);

DYMPLE_WUR ferr_t dymple_open_process_binary_raw(sys_channel_t** out_channel);

DYMPLE_DECLARATIONS_END;

#endif // _DYMPLE_IMAGES_H_
