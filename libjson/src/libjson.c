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

#include <libjson/libjson.h>

ferr_t json_parse_string(const char* string, bool json5, json_object_t** out_object) {
	return json_parse_string_n(string, simple_strlen(string), json5, out_object);
};

ferr_t json_parse_string_n(const char* string, size_t string_length, bool json5, json_object_t** out_object) {
	// TODO
	return ferr_unsupported;
};

ferr_t json_parse_file(sys_file_t* file, bool json5, json_object_t** out_object) {
	// TODO
	return ferr_unsupported;
};
