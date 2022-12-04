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

#include <libsys/paths.h>
#include <libsys/mempool.h>
#include <libsimple/libsimple.h>

#define MAX_COMPONENTS 4096
#define MAX_ONSTACK_PATHS 16

ferr_t sys_path_component_first_n(const char* path, size_t path_length, sys_path_component_t* out_component) {
	const char* first_component = path;
	size_t first_component_length = path_length;
	const char* next_slash;

	if (!path || !out_component) {
		return ferr_invalid_argument;
	}

	while (first_component_length > 0 && *first_component == '/') {
		++first_component;
		--first_component_length;
	}

	if (first_component_length == 0) {
		return ferr_permanent_outage;
	}

	next_slash = simple_strnchr(first_component, '/', first_component_length);

	if (next_slash) {
		first_component_length = next_slash - first_component;
	}

	out_component->entire_path = path;
	out_component->entire_path_length = path_length;
	out_component->component = first_component;
	out_component->length = first_component_length;

	return ferr_ok;
};

ferr_t sys_path_component_first(const char* path, sys_path_component_t* out_component) {
	return sys_path_component_first_n(path, simple_strlen(path), out_component);
};

ferr_t sys_path_component_next(sys_path_component_t* in_out_component) {
	const char* next_component;
	size_t next_component_length;
	const char* next_slash;

	if (!in_out_component) {
		return ferr_invalid_argument;
	}

	next_component = in_out_component->component + in_out_component->length;
	next_component_length = (in_out_component->entire_path + in_out_component->entire_path_length) - next_component;

	while (next_component_length > 0 && *next_component == '/') {
		++next_component;
		--next_component_length;
	}

	if (next_component_length == 0) {
		return ferr_permanent_outage;
	}

	next_slash = simple_strnchr(next_component, '/', next_component_length);

	if (next_slash) {
		next_component_length = next_slash - next_component;
	}

	in_out_component->component = next_component;
	in_out_component->length = next_component_length;

	return ferr_ok;
};

ferr_t sys_path_join(char* out_buffer, size_t buffer_size, size_t* out_required_buffer_size, ...) {
	va_list args;
	va_start(args, out_required_buffer_size);
	ferr_t status = sys_path_join_v(out_buffer, buffer_size, out_required_buffer_size, args);
	va_end(args);
	return status;
};

ferr_t sys_path_join_v(char* out_buffer, size_t buffer_size, size_t* out_required_buffer_size, va_list layers) {
	const char* layer = va_arg(layers, const char*);
	sys_path_t layer_paths[MAX_ONSTACK_PATHS];
	size_t i = 0;

	for (; i < sizeof(layer_paths) / sizeof(*layer_paths) && layer != NULL; ++i, (layer = va_arg(layers, const char*))) {
		layer_paths[i].contents = layer;
		layer_paths[i].length = simple_strlen(layer);
	}

	if (i == sizeof(layer_paths) / sizeof(*layer_paths)) {
		layer = va_arg(layers, const char*);
		if (layer != NULL) {
			return ferr_invalid_argument;
		}
	}

	return sys_path_join_s(&layer_paths[0], i, out_buffer, buffer_size, out_required_buffer_size);
};

ferr_t sys_path_join_n(char* out_buffer, size_t buffer_size, size_t* out_required_buffer_size, ...) {
	va_list args;
	va_start(args, out_required_buffer_size);
	ferr_t status = sys_path_join_nv(out_buffer, buffer_size, out_required_buffer_size, args);
	va_end(args);
	return status;
};

ferr_t sys_path_join_nv(char* out_buffer, size_t buffer_size, size_t* out_required_buffer_size, va_list layers) {
	size_t layer_length = va_arg(layers, size_t);
	const char* layer = va_arg(layers, const char*);
	sys_path_t layer_paths[MAX_ONSTACK_PATHS];
	size_t i = 0;

	for (; i < sizeof(layer_paths) / sizeof(*layer_paths) && layer != NULL; ++i, (layer_length = va_arg(layers, size_t)), (layer = va_arg(layers, const char*))) {
		layer_paths[i].contents = layer;
		layer_paths[i].length = layer_length;
	}

	if (i == sizeof(layer_paths) / sizeof(*layer_paths)) {
		layer_length = va_arg(layers, size_t);
		layer = va_arg(layers, const char*);
		if (layer != NULL) {
			return ferr_invalid_argument;
		}
	}

	return sys_path_join_s(&layer_paths[0], i, out_buffer, buffer_size, out_required_buffer_size);
};

ferr_t sys_path_join_a(const char** layers, size_t layer_count, char* out_buffer, size_t buffer_size, size_t* out_required_buffer_size) {
	sys_path_t layer_paths[MAX_ONSTACK_PATHS];
	size_t i = 0;

	if (layer_count > sizeof(layer_paths) / sizeof(*layer_paths)) {
		return ferr_invalid_argument;
	}

	for (; i < sizeof(layer_paths) / sizeof(*layer_paths) && i < layer_count; ++i) {
		layer_paths[i].contents = layers[i];
		layer_paths[i].length = simple_strlen(layers[i]);
	}

	return sys_path_join_s(&layer_paths[0], i, out_buffer, buffer_size, out_required_buffer_size);
};

ferr_t sys_path_join_na(const char** layers, const size_t* layer_lengths, size_t layer_count, char* out_buffer, size_t buffer_size, size_t* out_required_buffer_size) {
	sys_path_t layer_paths[MAX_ONSTACK_PATHS];
	size_t i = 0;

	if (layer_count > sizeof(layer_paths) / sizeof(*layer_paths)) {
		return ferr_invalid_argument;
	}

	for (; i < sizeof(layer_paths) / sizeof(*layer_paths) && i < layer_count; ++i) {
		layer_paths[i].contents = layers[i];
		layer_paths[i].length = layer_lengths[i];
	}

	return sys_path_join_s(&layer_paths[0], i, out_buffer, buffer_size, out_required_buffer_size);
};

//
// base implementation
//
ferr_t sys_path_join_s(const sys_path_t* layers, size_t layer_count, char* out_buffer, size_t buffer_size, size_t* out_required_buffer_size) {
	size_t required_size = 0;
	char components_to_keep[MAX_COMPONENTS / 8] = {0};
	size_t components_to_keep_index = 0;
	bool is_first_component = true;

	for (size_t layer_index = 0; layer_index < layer_count; ++layer_index) {
		const sys_path_t* layer = &layers[layer_index];
		sys_path_component_t component;

		if (layer->length == 0) {
			continue;
		}

		if (!layer->contents) {
			return ferr_invalid_argument;
		}

		if (layer->contents[0] == '/') {
			// if the first item is a slash, this is an absolute path,
			// so skip all the previous components
			simple_memset(&components_to_keep[0], 0, sizeof(components_to_keep));
		}

		// first, iterate through the components to find which are kept
		for (ferr_t path_status = sys_path_component_first_n(layer->contents, layer->length, &component); path_status == ferr_ok; path_status = sys_path_component_next(&component)) {
			// skip '.'
			if (component.length == 1 && component.component[0] == '.') {
				continue;
			}

			// handle '..'
			if (component.length == 2 && component.component[0] == '.' && component.component[1] == '.') {
				// okay, so we want to skip a component

				// skip the first component we find searching backwards
				for (size_t j = components_to_keep_index; j > 0; --j) {
					if (components_to_keep[(j - 1) / 8] & (1 << ((j - 1) % 8))) {
						components_to_keep[(j - 1) / 8] &= ~(1 << ((j - 1) % 8));
						break;
					}
				}

				continue;
			}

			if (components_to_keep_index >= MAX_COMPONENTS) {
				return ferr_invalid_argument;
			}

			components_to_keep[components_to_keep_index / 8] |= 1 << (components_to_keep_index % 8);

			++components_to_keep_index;
		}
	}

	components_to_keep_index = 0;

	for (size_t layer_index = 0; layer_index < layer_count; ++layer_index) {
		const sys_path_t* layer = &layers[layer_index];
		sys_path_component_t component;

		if (layer->length == 0) {
			continue;
		}

		if (layer->contents[0] == '/' && required_size == 0) {
			// if the first item is a slash and we're at the start of the output path, this is an absolute path, so prepend a slash
			if (out_buffer && buffer_size > 0) {
				out_buffer[0] = '/';
			}
			++required_size;
		}

		// now actually output the components we want to keep
		for (ferr_t path_status = sys_path_component_first_n(layer->contents, layer->length, &component); path_status == ferr_ok; path_status = sys_path_component_next(&component)) {
			// skip '.'
			if (component.length == 1 && component.component[0] == '.') {
				continue;
			}

			// skip '..'
			if (component.length == 2 && component.component[0] == '.' && component.component[1] == '.') {
				continue;
			}

			// skip this component if we need to
			if ((components_to_keep[components_to_keep_index / 8] & (1 << (components_to_keep_index % 8))) == 0) {
				++components_to_keep_index;
				continue;
			}

			if (is_first_component) {
				is_first_component = false;
			} else {
				if (out_buffer && buffer_size > required_size) {
					out_buffer[required_size] = '/';
				}
				++required_size;
			}

			size_t remaining_buffer_size = (required_size < buffer_size) ? (buffer_size - required_size) : 0;
			if (out_buffer && remaining_buffer_size > 0) {
				simple_memcpy(&out_buffer[required_size], component.component, simple_min(component.length, remaining_buffer_size));
			}
			required_size += component.length;

			++components_to_keep_index;
		}
	}

	if (out_required_buffer_size) {
		*out_required_buffer_size = required_size;
	}
	return (buffer_size >= required_size) ? ferr_ok : ferr_too_big;
};

ferr_t sys_path_join_allocate(char** out_buffer, size_t* out_buffer_size, ...) {
	va_list args;
	va_start(args, out_buffer_size);
	ferr_t status = sys_path_join_allocate_v(out_buffer, out_buffer_size, args);
	va_end(args);
	return status;
};

ferr_t sys_path_join_allocate_v(char** out_buffer, size_t* out_buffer_size, va_list layers) {
	const char* layer = va_arg(layers, const char*);
	sys_path_t layer_paths[MAX_ONSTACK_PATHS];
	size_t i = 0;

	for (; i < sizeof(layer_paths) / sizeof(*layer_paths) && layer != NULL; ++i, (layer = va_arg(layers, const char*))) {
		layer_paths[i].contents = layer;
		layer_paths[i].length = simple_strlen(layer);
	}

	if (i == sizeof(layer_paths) / sizeof(*layer_paths)) {
		layer = va_arg(layers, const char*);
		if (layer != NULL) {
			return ferr_invalid_argument;
		}
	}

	return sys_path_join_allocate_s(&layer_paths[0], i, out_buffer, out_buffer_size);
};

ferr_t sys_path_join_allocate_n(char** out_buffer, size_t* out_buffer_size, ...) {
	va_list args;
	va_start(args, out_buffer_size);
	ferr_t status = sys_path_join_allocate_nv(out_buffer, out_buffer_size, args);
	va_end(args);
	return status;
};

ferr_t sys_path_join_allocate_nv(char** out_buffer, size_t* out_buffer_size, va_list layers) {
	size_t layer_length = va_arg(layers, size_t);
	const char* layer = va_arg(layers, const char*);
	sys_path_t layer_paths[MAX_ONSTACK_PATHS];
	size_t i = 0;

	for (; i < sizeof(layer_paths) / sizeof(*layer_paths) && layer != NULL; ++i, (layer_length = va_arg(layers, size_t)), (layer = va_arg(layers, const char*))) {
		layer_paths[i].contents = layer;
		layer_paths[i].length = layer_length;
	}

	if (i == sizeof(layer_paths) / sizeof(*layer_paths)) {
		layer_length = va_arg(layers, size_t);
		layer = va_arg(layers, const char*);
		if (layer != NULL) {
			return ferr_invalid_argument;
		}
	}

	return sys_path_join_allocate_s(&layer_paths[0], i, out_buffer, out_buffer_size);
};

ferr_t sys_path_join_allocate_a(const char** layers, size_t layer_count, char** out_buffer, size_t* out_buffer_size) {
	sys_path_t layer_paths[MAX_ONSTACK_PATHS];
	size_t i = 0;

	if (layer_count > sizeof(layer_paths) / sizeof(*layer_paths)) {
		return ferr_invalid_argument;
	}

	for (; i < sizeof(layer_paths) / sizeof(*layer_paths) && i < layer_count; ++i) {
		layer_paths[i].contents = layers[i];
		layer_paths[i].length = simple_strlen(layers[i]);
	}

	return sys_path_join_allocate_s(&layer_paths[0], i, out_buffer, out_buffer_size);
};

ferr_t sys_path_join_allocate_na(const char** layers, const size_t* layer_lengths, size_t layer_count, char** out_buffer, size_t* out_buffer_size) {
	sys_path_t layer_paths[MAX_ONSTACK_PATHS];
	size_t i = 0;

	if (layer_count > sizeof(layer_paths) / sizeof(*layer_paths)) {
		return ferr_invalid_argument;
	}

	for (; i < sizeof(layer_paths) / sizeof(*layer_paths) && i < layer_count; ++i) {
		layer_paths[i].contents = layers[i];
		layer_paths[i].length = layer_lengths[i];
	}

	return sys_path_join_allocate_s(&layer_paths[0], i, out_buffer, out_buffer_size);
};

ferr_t sys_path_join_allocate_s(const sys_path_t* layers, size_t layer_count, char** out_buffer, size_t* out_buffer_size) {
	size_t length = 0;
	char* buffer = NULL;
	ferr_t status = ferr_ok;

	if (!out_buffer) {
		status = ferr_invalid_argument;
		goto out;
	}

	status = sys_path_join_s(layers, layer_count, NULL, 0, &length);
	if (status != ferr_too_big) {
		goto out;
	}

	status = sys_mempool_allocate(length + 1, NULL, (void*)&buffer);
	if (status != ferr_ok) {
		goto out;
	}

	status = sys_path_join_s(layers, layer_count, buffer, length, &length);

out:
	if (status == ferr_ok) {
		buffer[length] = '\0';
		*out_buffer = buffer;
		if (out_buffer_size) {
			*out_buffer_size = length;
		}
	} else {
		if (out_buffer) {
			LIBSYS_WUR_IGNORE(sys_mempool_free(out_buffer));
		}
	}
	return status;
};

ferr_t sys_path_normalize(const char* path, char* out_buffer, size_t buffer_size, size_t* out_required_buffer_size) {
	return sys_path_normalize_n(path, simple_strlen(path), out_buffer, buffer_size, out_required_buffer_size);
};

ferr_t sys_path_normalize_n(const char* path, size_t path_length, char* out_buffer, size_t buffer_size, size_t* out_required_buffer_size) {
	sys_path_t path_struct = {
		.contents = path,
		.length = path_length,
	};
	return sys_path_normalize_s(&path_struct, out_buffer, buffer_size, out_required_buffer_size);
};

//
// base implementation
//
ferr_t sys_path_normalize_s(const sys_path_t* path, char* out_buffer, size_t buffer_size, size_t* out_required_buffer_size) {
	return sys_path_join_s(path, 1, out_buffer, buffer_size, out_required_buffer_size);
};

ferr_t sys_path_normalize_allocate(const char* path, char** out_buffer, size_t* out_buffer_size) {
	return sys_path_normalize_allocate_n(path, simple_strlen(path), out_buffer, out_buffer_size);
};

ferr_t sys_path_normalize_allocate_n(const char* path, size_t path_length, char** out_buffer, size_t* out_buffer_size) {
	sys_path_t path_struct = {
		.contents = path,
		.length = path_length,
	};
	return sys_path_normalize_allocate_s(&path_struct, out_buffer, out_buffer_size);
};

ferr_t sys_path_normalize_allocate_s(const sys_path_t* path, char** out_buffer, size_t* out_buffer_size) {
	size_t length = 0;
	char* buffer = NULL;
	ferr_t status = ferr_ok;

	if (!out_buffer) {
		status = ferr_invalid_argument;
		goto out;
	}

	status = sys_path_normalize_s(path, NULL, 0, &length);
	if (status != ferr_too_big) {
		goto out;
	}

	status = sys_mempool_allocate(length + 1, NULL, (void*)&buffer);
	if (status != ferr_ok) {
		goto out;
	}

	status = sys_path_normalize_s(path, buffer, length, &length);

out:
	if (status == ferr_ok) {
		buffer[length] = '\0';
		*out_buffer = buffer;
		if (out_buffer_size) {
			*out_buffer_size = length;
		}
	} else {
		if (out_buffer) {
			LIBSYS_WUR_IGNORE(sys_mempool_free(out_buffer));
		}
	}
	return status;
};

ferr_t sys_path_file_name(const char* path, bool skip_dot, const char** out_start, size_t* out_length) {
	return sys_path_file_name_n(path, simple_strlen(path), skip_dot, out_start, out_length);
};

ferr_t sys_path_file_name_n(const char* path, size_t path_length, bool skip_dot, const char** out_start, size_t* out_length) {
	sys_path_t path_struct = {
		.contents = path,
		.length = path_length,
	};
	return sys_path_file_name_s(&path_struct, skip_dot, out_start, out_length);
};

ferr_t sys_path_file_name_s(const sys_path_t* path, bool skip_dot, const char** out_start, size_t* out_length) {
	sys_path_component_t component;
	size_t i = 0;
	size_t target_index = 0;

	if (!path) {
		return ferr_invalid_argument;
	}

	// TODO: optimize this by iterating from the back instead.
	//       as long as we find '..' (or '.', if we're told to skip it), we go to the previous component.
	//       as soon as we find a component that isn't '..' (or '.', if we're told to skip it), we return it.

	for (ferr_t path_status = sys_path_component_first_n(path->contents, path->length, &component); path_status == ferr_ok; (path_status = sys_path_component_next(&component)), (++i)) {
		if (skip_dot && component.length == 1 && component.component[0] == '.') {
			continue;
		}

		if (component.length == 2 && component.component[0] == '.' && component.component[1] == '.') {
			if (target_index > 0) {
				--target_index;
			}
			continue;
		}

		target_index = i;
	}

	i = 0;

	for (ferr_t path_status = sys_path_component_first_n(path->contents, path->length, &component); path_status == ferr_ok; (path_status = sys_path_component_next(&component)), (++i)) {
		if (i == target_index) {
			if (out_start) {
				*out_start = component.component;
			}
			if (out_length) {
				*out_length = component.length;
			}
			return ferr_ok;
		}
	}

	return ferr_no_such_resource;
};

ferr_t sys_path_extension_name(const char* path, bool skip_dot, const char** out_start, size_t* out_length) {
	return sys_path_extension_name_n(path, simple_strlen(path), skip_dot, out_start, out_length);
};

ferr_t sys_path_extension_name_n(const char* path, size_t path_length, bool skip_dot, const char** out_start, size_t* out_length) {
	sys_path_t path_struct = {
		.contents = path,
		.length = path_length,
	};
	return sys_path_extension_name_s(&path_struct, skip_dot, out_start, out_length);
};

ferr_t sys_path_extension_name_s(const sys_path_t* path, bool skip_dot, const char** out_start, size_t* out_length) {
	sys_path_t file_name = {0};
	ferr_t status = sys_path_file_name_s(path, skip_dot, &file_name.contents, &file_name.length);
	const char* start = NULL;

	if (status != ferr_ok) {
		goto out;
	}

	start = simple_strnchr(file_name.contents, '.', file_name.length);
	if (!start) {
		status = ferr_no_such_resource;
		goto out;
	}

	if (out_start) {
		*out_start = start;
	}

	if (out_length) {
		*out_length = file_name.length - (start - file_name.contents);
	}

out:
	return status;
};

bool sys_path_is_absolute(const char* path) {
	return sys_path_is_absolute_n(path, simple_strlen(path));
};

bool sys_path_is_absolute_n(const char* path, size_t path_length) {
	sys_path_t path_struct = {
		.contents = path,
		.length = path_length,
	};
	return sys_path_is_absolute_s(&path_struct);
};

bool sys_path_is_absolute_s(const sys_path_t* path) {
	return path && path->length > 0 && path->contents[0] == '/';
};
