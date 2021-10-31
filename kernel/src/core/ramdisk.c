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

/**
 * @file
 *
 * Ramdisk processing and access.
 */

#include <ferro/core/ramdisk.h>
#include <ferro/core/vfs.backend.h>
#include <ferro/core/panic.h>
#include <ferro/core/mempool.h>

#include <libsimple/libsimple.h>

FERRO_STRUCT(ferro_ramdisk_node_descriptor) {
	fvfs_descriptor_t descriptor;
	ferro_ramdisk_directory_entry_t* entry;
};

static ferr_t vfs_ramdisk_open(void* context, fvfs_mount_t* mount, const char* path, size_t path_length, fvfs_descriptor_flags_t flags, fvfs_descriptor_t** out_descriptor);
static ferr_t vfs_ramdisk_close(void* context, fvfs_descriptor_t* descriptor);
static ferr_t vfs_ramdisk_list_children_init(void* context, fvfs_descriptor_t* descriptor, fvfs_path_t* out_child_array, size_t child_array_count, bool absolute, size_t* out_listed_count, fvfs_list_children_context_t* out_context);
static ferr_t vfs_ramdisk_list_children(void* context, fvfs_descriptor_t* descriptor, fvfs_path_t* in_out_child_array, size_t child_array_count, bool absolute, size_t* in_out_listed_count, fvfs_list_children_context_t* in_out_context);
static ferr_t vfs_ramdisk_list_children_finish(void* context, fvfs_descriptor_t* descriptor, fvfs_path_t* child_array, size_t listed_count, fvfs_list_children_context_t* in_out_context);
static ferr_t vfs_ramdisk_copy_path(void* context, fvfs_descriptor_t* descriptor, bool absolute, char* out_path_buffer, size_t path_buffer_size, size_t* out_length);
static ferr_t vfs_ramdisk_copy_info(void* context, fvfs_descriptor_t* descriptor, fvfs_node_info_t* out_info);
static ferr_t vfs_ramdisk_read(void* context, fvfs_descriptor_t* descriptor, size_t offset, void* buffer, size_t buffer_size, size_t* out_read_count);

static fvfs_backend_t vfs_ramdisk_backend = {
	.open = vfs_ramdisk_open,
	.close = vfs_ramdisk_close,
	.list_children_init = vfs_ramdisk_list_children_init,
	.list_children = vfs_ramdisk_list_children,
	.list_children_finish = vfs_ramdisk_list_children_finish,
	.copy_path = vfs_ramdisk_copy_path,
	.copy_info = vfs_ramdisk_copy_info,
	.read = vfs_ramdisk_read,
};

static ferro_ramdisk_t* ramdisk = NULL;

static const char* string_table = NULL;
static size_t string_table_length = 0;

static ferro_ramdisk_directory_entry_t* entry_array = NULL;

static uint8_t* data = NULL;
static size_t data_size = 0;

FERRO_ALWAYS_INLINE const char* find_string(uint64_t offset) {
	if (!string_table || offset > string_table_length) {
		return NULL;
	}

	return &string_table[offset];
};

FERRO_ALWAYS_INLINE bool entry_is_directory(const ferro_ramdisk_directory_entry_t* entry) {
	return (entry->flags & ferro_ramdisk_directory_entry_flag_is_directory) != 0;
};

FERRO_ALWAYS_INLINE const char* entry_name(const ferro_ramdisk_directory_entry_t* entry) {
	return find_string(entry->name_offset);
};

FERRO_ALWAYS_INLINE ferro_ramdisk_directory_entry_t* directory_children(const ferro_ramdisk_directory_entry_t* entry) {
	if (entry->contents_offset == UINT64_MAX) {
		return NULL;
	}
	return &entry_array[entry->contents_offset];
};

FERRO_ALWAYS_INLINE uint8_t* file_contents(const ferro_ramdisk_directory_entry_t* entry) {
	if (entry->contents_offset == UINT64_MAX) {
		return NULL;
	}
	return &data[entry->contents_offset];
};

FERRO_ALWAYS_INLINE ferro_ramdisk_directory_entry_t* entry_parent(const ferro_ramdisk_directory_entry_t* entry) {
	if (entry->parent_index == UINT64_MAX) {
		return NULL;
	}
	return &entry_array[entry->parent_index];
};

static ferro_ramdisk_directory_entry_t* entry_for_path(const char* path, size_t path_length) {
	fvfs_path_component_t component = {0};
	ferro_ramdisk_directory_entry_t* curr_entry = &entry_array[0];

	for (ferr_t status = fvfs_path_component_first_n(path, path_length, &component); status == ferr_ok; status = fvfs_path_component_next(&component)) {
		bool ok = false;

		if (!entry_is_directory(curr_entry)) {
			return NULL;
		}

		for (uint64_t i = 0; i < curr_entry->size; ++i) {
			ferro_ramdisk_directory_entry_t* child = &directory_children(curr_entry)[i];
			const char* child_name = entry_name(child);

			if (simple_strlen(child_name) != component.length || simple_strncmp(component.component, child_name, component.length) != 0) {
				continue;
			}

			curr_entry = child;
			ok = true;
			break;
		}

		if (!ok) {
			return NULL;
		}
	}

	return curr_entry;
};

void ferro_ramdisk_init(ferro_ramdisk_t* in_ramdisk) {
	char* ramdisk_content_start;

	if (!in_ramdisk) {
		return;
	}

	ramdisk = in_ramdisk;
	ramdisk_content_start = (void*)&in_ramdisk->section_headers[in_ramdisk->section_count];

	for (uint64_t i = 0; i < in_ramdisk->section_count; ++i) {
		ferro_ramdisk_section_header_t* header = &in_ramdisk->section_headers[i];

		switch (header->type) {
			case ferro_ramdisk_section_type_string_table: {
				string_table = &ramdisk_content_start[header->offset];
				string_table_length = header->length;
			} break;

			case ferro_ramdisk_section_type_data: {
				data = (void*)&ramdisk_content_start[header->offset];
				data_size = header->length;
			} break;

			case ferro_ramdisk_section_type_directories: {
				if (header->length == 0 || !FERRO_IS_ALIGNED(header->length, sizeof(ferro_ramdisk_directory_entry_t))) {
					fpanic("Invalid ramdisk: directory entry section must contain at least one directory entry and its length must be a multiple of the directory entry structure size");
				}

				entry_array = (void*)&ramdisk_content_start[header->offset];

				if (!entry_is_directory(&entry_array[0])) {
					fpanic("Invalid ramdisk: root directory entry must be a directory");
				}

				if (entry_array[0].name_offset != UINT64_MAX) {
					fpanic("Invalid ramdisk: root directory entry must not have a name");
				}
			} break;
		}
	}

	if (fvfs_mount("/", 1, &vfs_ramdisk_backend, ramdisk) != ferr_ok) {
		fpanic("Failed to mount ramdisk");
	}
};

static ferr_t vfs_ramdisk_open(void* context, fvfs_mount_t* mount, const char* path, size_t path_length, fvfs_descriptor_flags_t flags, fvfs_descriptor_t** out_descriptor) {
	ferro_ramdisk_directory_entry_t* entry = entry_for_path(path, path_length);
	ferro_ramdisk_node_descriptor_t* desc = NULL;
	ferr_t status = ferr_ok;
	bool inited = false;

	if (!entry) {
		status = ferr_no_such_resource;
		goto out;
	}

	if (fmempool_allocate(sizeof(ferro_ramdisk_node_descriptor_t), NULL, (void*)&desc) != ferr_ok) {
		status = ferr_temporary_outage;
		goto out;
	}

	status = fvfs_descriptor_init(&desc->descriptor, mount, flags);
	if (status != ferr_ok) {
		goto out;
	}

	inited = true;

	desc->entry = entry;

out:
	if (status != ferr_ok) {
		if (inited) {
			fvfs_descriptor_destroy(&desc->descriptor);
		}
		if (desc) {
			fpanic_status(fmempool_free(desc));
		}
	} else {
		*out_descriptor = &desc->descriptor;
	}
	return status;
};

static ferr_t vfs_ramdisk_close(void* context, fvfs_descriptor_t* descriptor) {
	ferro_ramdisk_node_descriptor_t* desc = (void*)descriptor;

	fvfs_descriptor_destroy(descriptor);

	fpanic_status(fmempool_free(desc));

	return ferr_ok;
};

static ferr_t vfs_ramdisk_list_children_init(void* context, fvfs_descriptor_t* descriptor, fvfs_path_t* out_child_array, size_t child_array_count, bool absolute, size_t* out_listed_count, fvfs_list_children_context_t* out_context) {
	ferro_ramdisk_node_descriptor_t* desc = (void*)descriptor;
	ferro_ramdisk_directory_entry_t* children;

	if (!entry_is_directory(desc->entry)) {
		return ferr_invalid_argument;
	}

	if (desc->entry->size == 0) {
		*out_listed_count = 0;
		*out_context = 0;
		return ferr_permanent_outage;
	}

	if (child_array_count == 0) {
		*out_listed_count = desc->entry->size;
		*out_context = *out_listed_count;
		return ferr_ok;
	}

	*out_listed_count = 0;

	children = directory_children(desc->entry);

	for (size_t i = 0; i < child_array_count && i < desc->entry->size; ++i) {
		ferro_ramdisk_directory_entry_t* child = &children[i];

		if (absolute) {
			size_t length = 0;
			ferro_ramdisk_directory_entry_t* curr = child;
			char* string;
			char* string_pos;
			while (curr && entry_name(curr)) {
				// `+1` for the slash
				length += simple_strlen(entry_name(curr)) + 1;
				curr = entry_parent(curr);
			}
			if (fmempool_allocate(length, NULL, (void*)&string) != ferr_ok) {
				// it might be okay that we failed to allocate;
				// outside the loop, we check whether we managed to allocate any at all.
				// if so, then we report success and let the caller use the partial results.
				// otherwise, if we had entries but couldn't allocate anything, then we return a temporary outage.
				break;
			}
			out_child_array[i].path = string;
			out_child_array[i].length = length;
			string_pos = string + length;
			curr = child;
			while (curr && entry_name(curr)) {
				size_t len = simple_strlen(entry_name(curr));
				string_pos -= len;
				simple_memcpy(string_pos, entry_name(curr), len);
				string_pos -= 1;
				string_pos[0] = '/';
				curr = entry_parent(curr);
			}
		} else {
			out_child_array[i].path = entry_name(child);
			out_child_array[i].length = simple_strlen(out_child_array[i].path);
		}
		++(*out_listed_count);
	}

	if (*out_listed_count == 0) {
		return ferr_temporary_outage;
	}

	*out_context = (absolute ? (1ULL << 63) : 0) | (*out_listed_count & 0x7fffffffffffffffULL);

	return ferr_ok;
};

static ferr_t vfs_ramdisk_list_children(void* context, fvfs_descriptor_t* descriptor, fvfs_path_t* in_out_child_array, size_t child_array_count, bool absolute, size_t* in_out_listed_count, fvfs_list_children_context_t* in_out_context) {
	ferro_ramdisk_node_descriptor_t* desc = (void*)descriptor;
	ferro_ramdisk_directory_entry_t* children;

	size_t in_position = *in_out_context & 0x7fffffffffffffffULL;
	bool was_absolute = !!(*in_out_context & (1ULL << 63));

	if (!entry_is_directory(desc->entry)) {
		return ferr_invalid_argument;
	}

	if (was_absolute) {
		for (size_t i = 0; i < *in_out_listed_count; ++i) {
			fpanic_status(fmempool_free((void*)in_out_child_array[i].path));
		}
	}

	if (desc->entry->size - in_position == 0) {
		*in_out_listed_count = 0;
		return ferr_permanent_outage;
	}

	if (child_array_count == 0) {
		*in_out_listed_count = desc->entry->size - in_position;
		*in_out_context = desc->entry->size;
		return ferr_ok;
	}

	*in_out_listed_count = 0;

	children = directory_children(desc->entry);

	for (size_t i = 0; i < child_array_count && i < desc->entry->size - in_position; ++i) {
		ferro_ramdisk_directory_entry_t* child = &children[i + in_position];

		if (absolute) {
			size_t length = 0;
			ferro_ramdisk_directory_entry_t* curr = child;
			char* string;
			char* string_pos;
			while (curr && entry_name(curr)) {
				// `+1` for the slash
				length += simple_strlen(entry_name(curr)) + 1;
				curr = entry_parent(curr);
			}
			if (fmempool_allocate(length, NULL, (void*)&string) != ferr_ok) {
				// it might be okay that we failed to allocate;
				// outside the loop, we check whether we managed to allocate any at all.
				// if so, then we report success and let the caller use the partial results.
				// otherwise, if we had entries but couldn't allocate anything, then we return a temporary outage.
				break;
			}
			in_out_child_array[i].path = string;
			in_out_child_array[i].length = length;
			string_pos = string + length;
			curr = child;
			while (curr && entry_name(curr)) {
				size_t len = simple_strlen(entry_name(curr));
				string_pos -= len;
				simple_memcpy(string_pos, entry_name(curr), len);
				string_pos -= 1;
				string_pos[0] = '/';
				curr = entry_parent(curr);
			}
		} else {
			in_out_child_array[i].path = entry_name(child);
			in_out_child_array[i].length = simple_strlen(in_out_child_array[i].path);
		}
		++(*in_out_listed_count);
	}

	if (*in_out_listed_count == 0) {
		return ferr_temporary_outage;
	}

	*in_out_context = (absolute ? (1ULL << 63) : 0) | ((in_position + *in_out_listed_count) & 0x7fffffffffffffffULL);

	return ferr_ok;
};

static ferr_t vfs_ramdisk_list_children_finish(void* context, fvfs_descriptor_t* descriptor, fvfs_path_t* child_array, size_t listed_count, fvfs_list_children_context_t* in_out_context) {
	ferro_ramdisk_node_descriptor_t* desc = (void*)descriptor;

	size_t in_position = *in_out_context & 0x7fffffffffffffffULL;
	bool was_absolute = !!(*in_out_context & (1ULL << 63));

	if (!entry_is_directory(desc->entry)) {
		return ferr_invalid_argument;
	}

	if (was_absolute) {
		for (size_t i = 0; i < listed_count; ++i) {
			fpanic_status(fmempool_free((void*)child_array[i].path));
		}
	}

	*in_out_context = desc->entry->size;

	return ferr_ok;
};

static ferr_t vfs_ramdisk_copy_path(void* context, fvfs_descriptor_t* descriptor, bool absolute, char* out_path_buffer, size_t path_buffer_size, size_t* out_length) {
	size_t length = 0;
	ferro_ramdisk_node_descriptor_t* desc = (void*)descriptor;
	ferro_ramdisk_directory_entry_t* curr = desc->entry;

	if (absolute) {
		while (curr && entry_name(curr)) {
			// `+1` for the slash
			length += simple_strlen(entry_name(curr)) + 1;
			curr = entry_parent(curr);
		}
	} else {
		length = simple_strlen(entry_name(curr));
	}

	*out_length = length;

	if (length > path_buffer_size) {
		return ferr_too_big;
	}

	if (absolute) {
		char* string_pos = out_path_buffer + length;

		curr = desc->entry;

		while (curr && entry_name(curr)) {
			size_t len = simple_strlen(entry_name(curr));
			string_pos -= len;
			simple_memcpy(string_pos, entry_name(curr), len);
			string_pos -= 1;
			string_pos[0] = '/';
			curr = entry_parent(curr);
		}
	} else {
		simple_memcpy(out_path_buffer, entry_name(curr), length);
	}

	if (path_buffer_size >= length + 1) {
		out_path_buffer[length] = '\0';
	}

	return ferr_ok;
};

static ferr_t vfs_ramdisk_copy_info(void* context, fvfs_descriptor_t* descriptor, fvfs_node_info_t* out_info) {
	ferro_ramdisk_node_descriptor_t* desc = (void*)descriptor;

	out_info->type = entry_is_directory(desc->entry) ? fvfs_node_type_directory : fvfs_node_type_file;

	return ferr_ok;
};

static ferr_t vfs_ramdisk_read(void* context, fvfs_descriptor_t* descriptor, size_t offset, void* buffer, size_t buffer_size, size_t* out_read_count) {
	ferro_ramdisk_node_descriptor_t* desc = (void*)descriptor;
	size_t read_count = 0;

	if ((!buffer && buffer_size > 0) || entry_is_directory(desc->entry)) {
		return ferr_invalid_argument;
	}

	if (offset >= desc->entry->size) {
		return ferr_permanent_outage;
	}

	read_count = simple_min(desc->entry->size - offset, buffer_size);;

	simple_memcpy(buffer, file_contents(desc->entry) + offset, read_count);

	if (out_read_count) {
		*out_read_count = read_count;
	}

	return ferr_ok;
};
