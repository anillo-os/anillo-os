/*
 * This file is part of Anillo OS
 * Copyright (C) 2025 Anillo OS Developers
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

#include <libjson/libjson.private.h>

LIBJSON_STRUCT(json_dump_stack) {
	json_object_t* object;
	size_t indent_level;
	size_t index;
};

LIBJSON_STRUCT(json_dict_index_iterator_context) {
	size_t index;
	size_t required_index;
	const char* key;
	size_t key_length;
	json_object_t* value;
};

static ferr_t json_dump_write_char(char character, char** in_out_buffer_ptr, size_t* in_out_buffer_size, size_t* in_out_length) {
	char* buffer = *in_out_buffer_ptr;

	if (buffer) {
		if (1 > *in_out_buffer_size) {
			return ferr_too_big;
		}

		buffer[0] = character;

		*in_out_buffer_ptr += 1;
		*in_out_buffer_size -= 1;
	}

	*in_out_length += 1;
	return ferr_ok;
};

static ferr_t json_dump_write_string_n(const char* string, size_t length, char** in_out_buffer_ptr, size_t* in_out_buffer_size, size_t* in_out_length) {
	char* buffer = *in_out_buffer_ptr;

	if (length == 0) {
		return ferr_ok;
	}

	if (buffer) {
		if (length > *in_out_buffer_size) {
			return ferr_too_big;
		}

		simple_memcpy(buffer, string, length);

		*in_out_buffer_ptr += length;
		*in_out_buffer_size -= length;
	}

	*in_out_length += length;
	return ferr_ok;
};

static ferr_t json_dump_write_string(const char* string, char** in_out_buffer_ptr, size_t* in_out_buffer_size, size_t* in_out_length) {
	return json_dump_write_string_n(string, simple_strlen(string), in_out_buffer_ptr, in_out_buffer_size, in_out_length);
};

static ferr_t json_dump_write_indent(const char* indent, size_t indent_length, size_t indent_level, char** in_out_buffer_ptr, size_t* in_out_buffer_size, size_t* in_out_length) {
	ferr_t status = ferr_ok;

	if (!indent || indent_length == 0 || indent_level == 0) {
		return ferr_ok;
	}

	for (size_t i = 0; i < indent_level; ++i) {
		status = json_dump_write_string_n(indent, indent_length, in_out_buffer_ptr, in_out_buffer_size, in_out_length);
		if (status != ferr_ok) {
			break;
		}
	}

	return status;
};

static bool json_dict_index_iterator(void* _context, const char* key, size_t key_length, json_object_t* value) {
	json_dict_index_iterator_context_t* context = _context;

	if (context->index == context->required_index) {
		context->key = key;
		context->key_length = key_length;
		context->value = value;

		return false;
	} else {
		++context->index;
	}

	return true;
};

static ferr_t json_dump_write_string_object(const char* string, size_t length, char** in_out_buffer_ptr, size_t* in_out_buffer_size, size_t* in_out_length) {
	ferr_t status = ferr_ok;

	status = json_dump_write_char('"', in_out_buffer_ptr, in_out_buffer_size, in_out_length);
	if (status != ferr_ok) {
		goto out;
	}

	for (size_t i = 0; i < length; ++i) {
		switch (string[i]) {
			case '\\':
			case '\b':
			case '\f':
			case '\n':
			case '\r':
			case '\t':
			case '"': {
				const char* escaped = NULL;

				switch (string[i]) {
					case '\\': escaped = "\\\\"; break;
					case '\b': escaped = "\\b"; break;
					case '\f': escaped = "\\f"; break;
					case '\n': escaped = "\\n"; break;
					case '\r': escaped = "\\r"; break;
					case '\t': escaped = "\\t"; break;
					case '"': escaped = "\\\""; break;
				}

				status = json_dump_write_string_n(escaped, 2, in_out_buffer_ptr, in_out_buffer_size, in_out_length);
				if (status != ferr_ok) {
					goto out;
				}
			} break;

			default: {
				if (string[i] < 0x20) {
					// this is a control character and must be escaped
					char buffer[5];
					size_t buffer_length = 0;

					status = sys_format_out_buffer(buffer, sizeof(buffer), &buffer_length, "%04x", string[i]);
					if (status != ferr_ok) {
						goto out;
					}

					status = json_dump_write_string_n(buffer, buffer_length, in_out_buffer_ptr, in_out_buffer_size, in_out_length);
					if (status != ferr_ok) {
						goto out;
					}
				} else {
					// this can be written as-is
					status = json_dump_write_char(string[i], in_out_buffer_ptr, in_out_buffer_size, in_out_length);
					if (status != ferr_ok) {
						goto out;
					}
				}
			} break;
		}
	}

	status = json_dump_write_char('"', in_out_buffer_ptr, in_out_buffer_size, in_out_length);
	if (status != ferr_ok) {
		goto out;
	}

out:
	return status;
};

ferr_t json_dump_n(json_object_t* object, const char* indent, size_t indent_length, char* out_buffer, size_t buffer_size, size_t* out_length) {
	ferr_t status = ferr_ok;
	json_dump_stack_t* dump_stack = NULL;
	size_t dump_stack_size = 0;
	size_t length = 0;

	status = sys_mempool_allocate(sizeof(*dump_stack), NULL, (void*)&dump_stack);
	if (status != ferr_ok) {
		goto out;
	}

	dump_stack[0].object = object;
	dump_stack[0].indent_level = 0;
	dump_stack[0].index = 0;
	dump_stack_size = 1;

	while (dump_stack_size > 0) {
		json_dump_stack_t* current_dump = &dump_stack[dump_stack_size - 1];
		const json_object_class_t* obj_class = json_object_class(current_dump->object);
		bool pop_stack = false;

		if (obj_class == json_object_class_array() || obj_class == json_object_class_dict()) {
			bool is_array = obj_class == json_object_class_array();
			size_t obj_length = (is_array) ? json_array_length(current_dump->object) : json_dict_entries(current_dump->object);

			if (obj_length == 0) {
				status = json_dump_write_string_n(is_array ? "[]" : "{}", 2, &out_buffer, &buffer_size, &length);
				if (status != ferr_ok) {
					goto out;
				}
				pop_stack = true;
			} else if (current_dump->index == obj_length) {
				if (indent) {
					status = json_dump_write_char('\n', &out_buffer, &buffer_size, &length);
					if (status != ferr_ok) {
						goto out;
					}
				}

				status = json_dump_write_indent(indent, indent_length, current_dump->indent_level, &out_buffer, &buffer_size, &length);
				if (status != ferr_ok) {
					goto out;
				}

				status = json_dump_write_char(is_array ? ']' : '}', &out_buffer, &buffer_size, &length);
				if (status != ferr_ok) {
					goto out;
				}

				pop_stack = true;
			} else {
				json_dump_stack_t* new_dump = NULL;
				json_object_t* entry = NULL;

				if (current_dump->index == 0) {
					status = json_dump_write_string_n((indent) ? ((is_array) ? "[\n" : "{\n") : ((is_array) ? "[" : "{"), (indent) ? 2 : 1, &out_buffer, &buffer_size, &length);
					if (status != ferr_ok) {
						goto out;
					}
				} else {
					status = json_dump_write_string_n((indent) ? ",\n" : ",", (indent) ? 2 : 1, &out_buffer, &buffer_size, &length);
					if (status != ferr_ok) {
						goto out;
					}
				}

				status = json_dump_write_indent(indent, indent_length, current_dump->indent_level + 1, &out_buffer, &buffer_size, &length);
				if (status != ferr_ok) {
					goto out;
				}

				if (is_array) {
					entry = ((json_array_object_t*)current_dump->object)->objects[current_dump->index];
				} else {
					// for dictionaries, we use somewhat of a hack:
					// as long the dictionary (and the underlying ghmap) doesn't change, the iterator will always be called with the same keys and values in the same order
					// therefore, we can use an iterator that keeps track of how many times it's been called and then return the key and value for the needed index
					json_dict_index_iterator_context_t context = {
						.index = 0,
						.required_index = current_dump->index,
						.key = NULL,
						.key_length = 0,
						.value = NULL,
					};
					LIBJSON_WUR_IGNORE(json_dict_iterate(current_dump->object, json_dict_index_iterator, &context));
					fassert(context.key != NULL && context.value != NULL);
					entry = context.value;

					status = json_dump_write_string_object(context.key, context.key_length, &out_buffer, &buffer_size, &length);
					if (status != ferr_ok) {
						goto out;
					}

					status = json_dump_write_string_n((indent) ? ": " : ":", (indent) ? 2 : 1, &out_buffer, &buffer_size, &length);
					if (status != ferr_ok) {
						goto out;
					}
				}

				status = sys_mempool_reallocate(dump_stack, sizeof(*dump_stack) * (dump_stack_size + 1), NULL, (void*)&dump_stack);
				if (status != ferr_ok) {
					goto out;
				}
				current_dump = &dump_stack[dump_stack_size - 1];

				new_dump = &dump_stack[dump_stack_size];
				new_dump->object = entry;
				new_dump->indent_level = current_dump->indent_level + 1;
				new_dump->index = 0;
				++dump_stack_size;

				++current_dump->index;
			}
		} else if (obj_class == json_object_class_string()) {
			status = json_dump_write_string_object(json_string_contents(current_dump->object), json_string_length(current_dump->object), &out_buffer, &buffer_size, &length);
			if (status != ferr_ok) {
				goto out;
			}
			pop_stack = true;
		} else if (obj_class == json_object_class_bool()) {
			bool val = json_bool_value(current_dump->object);
			status = json_dump_write_string_n((val) ? "true" : "false", (val) ? 4 : 5, &out_buffer, &buffer_size, &length);
			if (status != ferr_ok) {
				goto out;
			}
		} else if (obj_class == json_object_class_null()) {
			status = json_dump_write_string_n("null", 4, &out_buffer, &buffer_size, &length);
			if (status != ferr_ok) {
				goto out;
			}
		} else if (obj_class == json_object_class_number()) {
			char buffer[40];
			size_t buffer_length = 0;

			if (json_number_is_integral(current_dump->object)) {
				// use the signed representation; more often than not, JSON will contain negative values rather than really large unsigned values
				// either way, the signed representation still represents the same value, so it should be safe to do this
				status = sys_format_out_buffer(buffer, sizeof(buffer), &buffer_length, "%lli", json_number_value_signed_integer(current_dump->object));
			} else {
				status = sys_format_out_buffer(buffer, sizeof(buffer), &buffer_length, "%f", json_number_value_float(current_dump->object));
			}

			if (status != ferr_ok) {
				goto out;
			}

			status = json_dump_write_string_n(buffer, buffer_length, &out_buffer, &buffer_size, &length);
			if (status != ferr_ok) {
				goto out;
			}
		}

		if (pop_stack) {
			status = sys_mempool_reallocate(dump_stack, sizeof(*dump_stack) * (dump_stack_size - 1), NULL, (void*)&dump_stack);
			if (status != ferr_ok) {
				goto out;
			}
			--dump_stack_size;
		}
	}

out:
	if (dump_stack) {
		LIBJSON_WUR_IGNORE(sys_mempool_free(dump_stack));
	}
	if (out_length) {
		*out_length = length;
	}
	return status;
};

ferr_t json_dump_allocate_n(json_object_t* object, const char* indent, size_t indent_length, char** out_buffer, size_t* out_length) {
	ferr_t status = ferr_ok;
	size_t length = 0;
	char* buffer = NULL;

	if (!out_buffer) {
		status = ferr_invalid_argument;
		goto out;
	}

	status = json_dump_n(object, indent, indent_length, NULL, 0, &length);
	if (status != ferr_ok) {
		goto out;
	}

	status = sys_mempool_allocate(length + 1, NULL, (void*)&buffer);
	if (status != ferr_ok) {
		goto out;
	}

	status = json_dump_n(object, indent, indent_length, buffer, length + 1, &length);

out:
	if (status == ferr_ok) {
		*out_buffer = buffer;
		if (out_length) {
			*out_length = length;
		}
	} else if (buffer) {
		LIBJSON_WUR_IGNORE(sys_mempool_free(buffer));
	}
	return status;
};

ferr_t json_dump(json_object_t* object, const char* indent, char* out_buffer, size_t buffer_size, size_t* out_length) {
	return json_dump_n(object, indent, (indent) ? simple_strlen(indent) : 0, out_buffer, buffer_size, out_length);
};

ferr_t json_dump_allocate(json_object_t* object, const char* indent, char** out_buffer, size_t* out_length) {
	return json_dump_allocate_n(object, indent, (indent) ? simple_strlen(indent) : 0, out_buffer, out_length);
};
