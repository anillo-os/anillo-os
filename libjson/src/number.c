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

#include <libjson/number.private.h>

static void json_number_destroy(json_object_t* obj) {
	json_number_object_t* number = (void*)obj;

	sys_object_destroy(obj);
};

static const json_object_class_t json_number_class = {
	LIBSYS_OBJECT_CLASS_INTERFACE(NULL),
	.destroy = json_number_destroy,
};

const json_object_class_t* json_object_class_number(void) {
	return &json_number_class;
};

ferr_t json_number_new_unsigned_integer(uint64_t value, json_number_t** out_number) {
	// TODO
	return ferr_unsupported;
};

ferr_t json_number_new_signed_integer(int64_t value, json_number_t** out_number) {
	// this cast should be safe and not affect the bits in any way
	return json_number_new_unsigned_integer((uint64_t)value, out_number);
};

ferr_t json_number_new_float(double value, json_number_t** out_number) {
	// TODO
	return ferr_unsupported;
};

uint64_t json_number_value_unsigned_integer(json_number_t* number) {
	// TODO
	return 0;
};

int64_t json_number_value_signed_integer(json_number_t* number) {
	// this cast should be safe and not affect the bits in any way
	return (int64_t)json_number_value_unsigned_integer(number);
};

double json_number_value_float(json_number_t* number) {
	// TODO
	return 0;
};

bool json_number_is_integral(json_number_t* number) {
	// TODO
	return false;
};
