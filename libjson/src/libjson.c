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

LIBJSON_STRUCT(json_object_stack) {
	json_object_t* object;
	char* pending_key;
	size_t pending_key_length;
};

ferr_t json_parse_string(const char* string, bool json5, json_object_t** out_object) {
	return json_parse_string_n(string, simple_strlen(string), json5, out_object);
};

ferr_t json_parse_file(sys_file_t* file, bool json5, json_object_t** out_object) {
	ferr_t status = ferr_ok;
	sys_file_info_t info;
	sys_data_t* data = NULL;

	status = sys_file_get_info(file, &info);
	if (status != ferr_ok) {
		goto out;
	}

	status = sys_file_read_data(file, 0, info.size, &data);
	if (status != ferr_ok) {
		goto out;
	}

	status = json_parse_string_n(sys_data_contents(data), sys_data_length(data), json5, out_object);

out:
	if (data) {
		sys_release(data);
	}
	return status;
};

ferr_t json_parse_string_object(const char* buffer, size_t buffer_length, bool json5, size_t* out_characters, char** out_string, size_t* out_string_length) {
	ferr_t status = ferr_ok;
	size_t offset = 0;
	size_t parsed_length = 0;
	char* parsed = NULL;
	bool singleQuoteString = false;

	if (buffer_length < 2) {
		// a string requires at least the opening and closing quotation marks
		status = ferr_invalid_argument;
		goto out;
	}

	if (json5 && buffer[offset] == '\'') {
		singleQuoteString = true;
	} else if (buffer[offset] != '"') {
		status = ferr_invalid_argument;
		goto out;
	}

	++offset;

	for (; offset < buffer_length; ++offset) {
		if () if (buffer[offset] == '"')
	}

	*out_characters = offset;
	*out_string = parsed;
	*out_string_length = parsed_length;

out:
	return status;
};

ferr_t json_parse_string_n(const char* string, size_t string_length, bool json5, json_object_t** out_object) {
	json_object_stack_t* object_stack = NULL;
	size_t object_stack_size = 0;
	ferr_t status = ferr_ok;
	json_object_t* result = NULL;

	while (string_length > 0) {

	}

	if (object_stack_size > 0) {
		status = ferr_invalid_argument;
		goto out;
	}

	if (!result) {
		status = ferr_invalid_argument;
		goto out;
	}

	*out_object = result;

out:
	if (object_stack_size > 0) {
		for (size_t i = 0; i < object_stack_size; ++i) {
			if (object_stack[i].pending_key) {
				LIBJSON_WUR_IGNORE(sys_mempool_free(object_stack[i].pending_key));
			}
			if (object_stack[i].object) {
				LIBJSON_WUR_IGNORE(json_release(object_stack[i].object));
			}
		}
		LIBJSON_WUR_IGNORE(sys_mempool_free(object_stack));
	}
	return status;
};
