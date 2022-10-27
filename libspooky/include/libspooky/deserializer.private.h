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

#ifndef _LIBSPOOKY_DESERIALIZER_PRIVATE_H_
#define _LIBSPOOKY_DESERIALIZER_PRIVATE_H_

#include <libspooky/base.h>
#include <libspooky/types.h>

LIBSPOOKY_DECLARATIONS_BEGIN;

LIBSPOOKY_STRUCT(spooky_deserializer) {
	const char* data;
	size_t length;
	size_t offset;
};

LIBSPOOKY_WUR ferr_t spooky_deserializer_init(spooky_deserializer_t* deserializer, const void* data, size_t length);

LIBSPOOKY_WUR ferr_t spooky_deserializer_skip(spooky_deserializer_t* deserializer, size_t offset, size_t* out_offset, size_t length);
LIBSPOOKY_WUR ferr_t spooky_deserializer_decode_integer(spooky_deserializer_t* deserializer, size_t offset, size_t* out_offset, void* out_value, size_t max_value_length, size_t* out_encoded_length, bool is_signed);
LIBSPOOKY_WUR ferr_t spooky_deserializer_decode_type(spooky_deserializer_t* deserializer, size_t offset, size_t* out_offset, size_t* out_length, spooky_type_t** out_type);

LIBSPOOKY_DECLARATIONS_END;

#endif // _LIBSPOOKY_DESERIALIZER_PRIVATE_H_
