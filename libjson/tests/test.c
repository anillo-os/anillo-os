/*
 * This file is part of Anillo OS
 * Copyright (C) 2024 Anillo OS Developers
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

#include "test.json.h"
#include "test.json5.h"

static void print_object(json_object_t* object, size_t indent);

static void print_indent(size_t indent) {
	while (indent > 0) {
		sys_console_log("\t");
		--indent;
	}
};

static bool print_object_dict_iterator(void* context, const char* key, size_t key_length, json_object_t* value) {
	size_t indent = (size_t)context;
	print_indent(indent + 1);
	sys_console_log_f("key:\"%.*s\" =\n", (int)key_length, key);
	print_object(value, indent + 2);
	return true;
};

static bool print_object_array_iterator(void* context, size_t index, json_object_t* value) {
	size_t indent = (size_t)context;
	print_object(value, indent + 1);
	return true;
};

static void print_object(json_object_t* object, size_t indent) {
	const json_object_class_t* obj_class = json_object_class(object);

	if (obj_class == json_object_class_dict()) {
		print_indent(indent);
		sys_console_log_f("dict:{\n");
		LIBJSON_WUR_IGNORE(json_dict_iterate(object, print_object_dict_iterator, (void*)indent));
		print_indent(indent);
		sys_console_log_f("}\n");
	} else if (obj_class == json_object_class_array()) {
		print_indent(indent);
		sys_console_log_f("array:[\n");
		LIBJSON_WUR_IGNORE(json_array_iterate(object, print_object_array_iterator, (void*)indent));
		print_indent(indent);
		sys_console_log_f("]\n");
	} else if (obj_class == json_object_class_null()) {
		print_indent(indent);
		sys_console_log_f("null\n");
	} else if (obj_class == json_object_class_bool()) {
		print_indent(indent);
		sys_console_log_f("bool:%s\n", json_bool_value(object) ? "true" : "false");
	} else if (obj_class == json_object_class_number()) {
		print_indent(indent);
		if (json_number_is_integral(object)) {
			sys_console_log_f("integer:(%llu or %lld)\n", json_number_value_unsigned_integer(object), json_number_value_signed_integer(object));
		} else {
			sys_console_log_f("float:%f\n", json_number_value_float(object));
		}
	} else if (obj_class == json_object_class_string()) {
		print_indent(indent);
		sys_console_log_f("string:\"%s\"\n", json_string_contents(object));
	} else {
		print_indent(indent);
		sys_console_log_f("<object of unknown class>\n");
	}
};

int main() {
	json_object_t* parsed = NULL;
	ferr_t status = ferr_ok;

	status = json_parse_string_n((const char*)&test_json_data[0], sizeof(test_json_data), false, &parsed);
	if (status != ferr_ok) {
		sys_console_log_f("Failed to parse JSON string: %d (%s: %s)\n", status, ferr_name(status), ferr_description(status));
		return 1;
	}

	sys_console_log_f("Successfully parsed JSON string!\n");
	print_object(parsed, 0);

	json_release(parsed);
	parsed = NULL;
	status = ferr_ok;

	status = json_parse_string_n((const char*)&test_json5_data[0], sizeof(test_json5_data), true, &parsed);
	if (status != ferr_ok) {
		sys_console_log_f("Failed to parse JSON5 string: %d (%s: %s)\n", status, ferr_name(status), ferr_description(status));
		return 1;
	}

	sys_console_log_f("Successfully parsed JSON5 string!\n");
	print_object(parsed, 0);

	json_release(parsed);
	parsed = NULL;
	status = ferr_ok;

	return 0;
};
