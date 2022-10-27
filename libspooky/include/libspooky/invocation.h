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

#ifndef _LIBSPOOKY_INVOCATION_H_
#define _LIBSPOOKY_INVOCATION_H_

#include <libspooky/base.h>
#include <libspooky/function.h>
#include <libeve/libeve.h>
#include <libspooky/data.h>

LIBSPOOKY_DECLARATIONS_BEGIN;

LIBSPOOKY_OBJECT_CLASS(invocation);

typedef void (*spooky_invocation_complete_f)(void* context, spooky_invocation_t* invocation, ferr_t status);

LIBSPOOKY_WUR ferr_t spooky_invocation_create(const char* name, size_t name_length, spooky_function_t* function, eve_channel_t* channel, spooky_invocation_t** out_invocation);

/**
 * Only valid for outgoing invocations. No longer valid after the first call to this function or spooky_invocation_execute_sync().
 */
LIBSPOOKY_WUR ferr_t spooky_invocation_execute_async(spooky_invocation_t* invocation, spooky_invocation_complete_f completion_callback, void* context);

/**
 * Only valid for outgoing invocations. No longer valid after the first call to this function or spooky_invocation_execute_async().
 */
LIBSPOOKY_WUR ferr_t spooky_invocation_execute_sync(spooky_invocation_t* invocation);

LIBSPOOKY_WUR ferr_t spooky_invocation_complete(spooky_invocation_t* invocation);

/**
 * Incoming invocations are those that are created by our peer and received locally.
 * Outgoing invocations are those created locally and sent to our peer.
 */
bool spooky_invocation_is_incoming(spooky_invocation_t* invocation);

#define SPOOKY_INVOCATION_BASIC_ACCESSOR(_type, _typecode) \
	LIBSPOOKY_WUR ferr_t spooky_invocation_get_ ## _typecode(spooky_invocation_t* invocation, size_t index, _type* out_value); \
	LIBSPOOKY_WUR ferr_t spooky_invocation_set_ ## _typecode(spooky_invocation_t* invocation, size_t index, _type value);

SPOOKY_INVOCATION_BASIC_ACCESSOR(uint8_t, u8);
SPOOKY_INVOCATION_BASIC_ACCESSOR(uint16_t, u16);
SPOOKY_INVOCATION_BASIC_ACCESSOR(uint32_t, u32);
SPOOKY_INVOCATION_BASIC_ACCESSOR(uint64_t, u64);

SPOOKY_INVOCATION_BASIC_ACCESSOR(int8_t, i8);
SPOOKY_INVOCATION_BASIC_ACCESSOR(int16_t, i16);
SPOOKY_INVOCATION_BASIC_ACCESSOR(int32_t, i32);
SPOOKY_INVOCATION_BASIC_ACCESSOR(int64_t, i64);

SPOOKY_INVOCATION_BASIC_ACCESSOR(bool, bool);

SPOOKY_INVOCATION_BASIC_ACCESSOR(float, f32);
SPOOKY_INVOCATION_BASIC_ACCESSOR(double, f64);

LIBSPOOKY_WUR ferr_t spooky_invocation_get_data(spooky_invocation_t* invocation, size_t index, bool retain, spooky_data_t** out_data);
LIBSPOOKY_WUR ferr_t spooky_invocation_set_data(spooky_invocation_t* invocation, size_t index, spooky_data_t* data);

LIBSPOOKY_WUR ferr_t spooky_invocation_get_structure(spooky_invocation_t* invocation, size_t index, bool retain_members, void* out_structure, size_t* in_out_structure_size);
LIBSPOOKY_WUR ferr_t spooky_invocation_set_structure(spooky_invocation_t* invocation, size_t index, const void* structure);

LIBSPOOKY_WUR ferr_t spooky_invocation_get_function(spooky_invocation_t* invocation, size_t index, spooky_function_implementation_f* out_function, void** out_context);
LIBSPOOKY_WUR ferr_t spooky_invocation_set_function(spooky_invocation_t* invocation, size_t index, spooky_function_implementation_f function, void* context);

/**
 * Only valid for `in` parameters on incoming invocations or `out` parameters on outgoing invocations.
 * For `out` parameters on outgoing invocations: only valid after the invocation has completed.
 * Can only be called once for each argument (and only on function-typed arguments).
 */
LIBSPOOKY_WUR ferr_t spooky_invocation_get_invocation(spooky_invocation_t* invocation, size_t index, spooky_invocation_t** out_invocation);

LIBSPOOKY_DECLARATIONS_END;

#endif // _LIBSPOOKY_INVOCATION_H_
