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

#ifndef _LIBSPOOKY_SERIALIZER_PRIVATE_H_
#define _LIBSPOOKY_SERIALIZER_PRIVATE_H_

#include <libspooky/base.h>
#include <libspooky/types.h>

LIBSPOOKY_DECLARATIONS_BEGIN;

LIBSPOOKY_STRUCT(spooky_serializer) {
	sys_channel_message_t* message;
	size_t length;
};

LIBSPOOKY_WUR ferr_t spooky_serializer_init(spooky_serializer_t* serializer);
LIBSPOOKY_WUR ferr_t spooky_serializer_finalize(spooky_serializer_t* serializer, sys_channel_message_t** out_message);

LIBSPOOKY_WUR ferr_t spooky_serializer_reserve(spooky_serializer_t* serializer, size_t offset, size_t* out_offset, size_t length);
LIBSPOOKY_WUR ferr_t spooky_serializer_encode_integer(spooky_serializer_t* serializer, size_t offset, size_t* out_offset, const void* value, size_t length, size_t* out_length, bool is_signed);
LIBSPOOKY_WUR ferr_t spooky_serializer_encode_type(spooky_serializer_t* serializer, size_t offset, size_t* out_offset, size_t* out_length, spooky_type_t* type);
LIBSPOOKY_WUR ferr_t spooky_serializer_encode_data(spooky_serializer_t* serializer, size_t offset, size_t* out_offset, size_t length, const void* data);
LIBSPOOKY_WUR ferr_t spooky_serializer_encode_data_object(spooky_serializer_t* serializer, size_t offset, size_t* out_offset, size_t* out_length, sys_data_t* data);
// consumes the caller's reference on the channel (which should be the only reference to it)
LIBSPOOKY_WUR ferr_t spooky_serializer_encode_channel(spooky_serializer_t* serializer, size_t offset, size_t* out_offset, size_t* out_length, sys_channel_t* channel);

LIBSPOOKY_DECLARATIONS_END;

#endif // _LIBSPOOKY_SERIALIZER_PRIVATE_H_
