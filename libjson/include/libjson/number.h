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

#ifndef _LIBJSON_NUMBER_H_
#define _LIBJSON_NUMBER_H_

#include <libjson/base.h>
#include <libjson/objects.h>

LIBJSON_DECLARATIONS_BEGIN;

LIBJSON_OBJECT_CLASS(number);

LIBJSON_WUR ferr_t json_number_new_unsigned_integer(uint64_t value, json_number_t** out_number);
LIBJSON_WUR ferr_t json_number_new_signed_integer(int64_t value, json_number_t** out_number);
LIBJSON_WUR ferr_t json_number_new_float(double value, json_number_t** out_number);

uint64_t json_number_value_unsigned_integer(json_number_t* number);
int64_t json_number_value_signed_integer(json_number_t* number);
double json_number_value_float(json_number_t* number);

bool json_number_is_integral(json_number_t* number);

LIBJSON_DECLARATIONS_END;

#endif // _LIBJSON_NUMBER_H_
