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

#include <dymple/resolution.private.h>
#include <libsimple/libsimple.h>
#include <dymple/images.private.h>
#include <dymple/leb128.h>
#include <libmacho/libmacho.h>
#include <dymple/log.h>
#include <dymple/api.private.h>

/**
 * Checks whether @p first is a prefix of @p second (whose length is @p second_length), but also measures the length of the first string.
 * Note that the first string MUST be null-terminated.
 * Also note that, if @p first is longer than @p second_max, this will return false.
 */
static bool check_if_prefix_and_output_length(const char* first, const char* second, size_t second_length, size_t* out_first_length) {
	size_t i = 0;
	size_t first_length = 0;
	bool result = true;

	for (; i < second_length; ++i) {
		if (first[i] == '\0') {
			break;
		} else if (first[i] != second[i]) {
			result = false;
			break;
		}
	}

	first_length = i + simple_strlen(&first[i]);

	if (first_length > second_length) {
		result = false;
	}

	if (out_first_length) {
		*out_first_length = first_length;
	}

	return result;
};

static ferr_t dymple_export_trie_find(dymple_image_t* image, const char* name, size_t name_length, const void** out_pointer) {
	ferr_t status = ferr_no_such_resource;
	const char* trie = image->export_trie;
	size_t offset = 0;
	size_t name_offset = 0;
	const void* pointer = NULL;

	size_t uleb_size = 0;

	dymple_log_debug(dymple_log_category_resolution, "Looking for %.*s in %.*s\n", (int)name_length, name, (int)image->name_length, image->name);

#if 0
	dymple_log_debug(dymple_log_category_resolution, "Export tree:");
	for (size_t i = 0; i < image->export_trie_size; ++i) {
		dymple_log_debug(dymple_log_category_resolution, " %x", trie[i]);
	}
	dymple_log_debug(dymple_log_category_resolution, "\n");
#endif

	while (offset < image->export_trie_size) {
		uintmax_t export_info_size = 0;
		uint8_t child_count = 0;
		bool found_child = false;

		// each trie node starts with a ULEB128 for the size of the (optional) export info
		status = dymple_leb128_decode_unsigned(&trie[offset], image->export_trie_size - offset, &export_info_size, &uleb_size);
		if (status != ferr_ok) {
			goto out;
		}
		offset += uleb_size;

		dymple_log_debug(dymple_log_category_resolution, "Export info size = %ju\n", export_info_size);

		if (export_info_size != 0 && name_offset == name_length) {
			dymple_log_debug(dymple_log_category_resolution, "Found target symbol at offset %zu\n", offset);
			// if the export info is present, this node represents a valid exported symbol.
			// if we've also reached the end of the name, then this is the symbol we're looking for.
			// let's return it.
			status = ferr_ok;
			pointer = &trie[offset];
			goto out;
		}

		// otherwise, we don't care about this node's export info.
		// let's skip it.
		offset += export_info_size;

		child_count = trie[offset];
		++offset;

		dymple_log_debug(dymple_log_category_resolution, "Child count = %u\n", child_count);

		// each child pointer is a null-terminated UTF-8 string for the next portion of the symbol name followed by its offset.
		// let's see if any of the children's names match the next portion of our target name.
		for (uint8_t i = 0; i < child_count; ++i) {
			size_t child_name_length = 0;
			bool correct_child = check_if_prefix_and_output_length(&trie[offset], &name[name_offset], name_length - name_offset, &child_name_length);
			uintmax_t child_offset = 0;

			dymple_log_debug(dymple_log_category_resolution, "Checking %s with %.*s = %s\n", &trie[offset], (int)(name_length - name_offset), &name[name_offset], correct_child ? "true" : "false");

			// skip the name string (along with the null-terminator)
			offset += child_name_length + 1;

			// the name string is immediately followed by a ULEB128 for the offset of the child
			status = dymple_leb128_decode_unsigned(&trie[offset], image->export_trie_size - offset, &child_offset, &uleb_size);
			if (status != ferr_ok) {
				goto out;
			}
			offset += uleb_size;

			if (correct_child) {
				// great, we found the right child!
				// let's continue searching there.
				offset = child_offset;
				found_child = true;
				name_offset += child_name_length;
				break;
			} else {
				// just skip over this child
				continue;
			}
		}

		if (!found_child) {
			// if we didn't find the right node in any of our children, the symbol isn't exported by this image
			status = ferr_no_such_resource;
			goto out;
		}
	}

out:
	if (status == ferr_ok && out_pointer) {
		*out_pointer = pointer;
	}
	return status;
};

extern void dymple_bind_stub_raw(void);

DYMPLE_STRUCT(dymple_override) {
	const char* name;
	size_t name_length;
	void* new_address;
};

#define STUB_REPLACEMENT(_name) { .name = "_" #_name, .name_length = sizeof("_" #_name) - 1, .new_address = _name }

static dymple_override_t libdymple_stubs[] = {
	{ .name = "dyld_stub_binder", .name_length = sizeof("dyld_stub_binder") - 1, .new_address = dymple_bind_stub_raw },
	STUB_REPLACEMENT(dymple_load_image_by_name),
	STUB_REPLACEMENT(dymple_load_image_by_name_n),
	STUB_REPLACEMENT(dymple_load_image_from_file),
	STUB_REPLACEMENT(dymple_find_loaded_image_by_name),
	STUB_REPLACEMENT(dymple_find_loaded_image_by_name_n),
	STUB_REPLACEMENT(dymple_resolve_symbol),
	STUB_REPLACEMENT(dymple_resolve_symbol_n),
	STUB_REPLACEMENT(dymple_open_process_binary_raw),
};

ferr_t dymple_resolve_export(dymple_image_t* image, const char* name, size_t name_length, dymple_symbol_t** out_export) {
	ferr_t status = ferr_ok;
	const void* export_info = NULL;
	dymple_symbol_t* export = NULL;
	uintmax_t raw_flags = 0;
	size_t offset = 0;
	size_t uleb_size = 0;
	macho_export_symbol_flags_t flags = 0;
	macho_export_symbol_kind_t kind = 0;
	uintmax_t value = 0;
	bool created = false;
	void* address = NULL;

	if (simple_ghmap_lookup(&image->exports_table, name, name_length, false, SIZE_MAX, NULL, (void*)&export, NULL) == ferr_ok) {
		goto out;
	}

	if (image->is_libdymple) {
		for (size_t i = 0; i < sizeof(libdymple_stubs) / sizeof(*libdymple_stubs); ++i) {
			dymple_override_t* stub_override = &libdymple_stubs[i];
			if (name_length == stub_override->name_length && simple_strncmp(name, stub_override->name, name_length) == 0) {
				// special case it
				status = simple_ghmap_lookup(&image->exports_table, name, name_length, true, SIZE_MAX, &created, (void*)&export, NULL);
				if (status != ferr_ok) {
					goto out;
				}

				if (!created) {
					goto out;
				}

				status = simple_ghmap_lookup_stored_key(&image->exports_table, name, name_length, (void*)&export->name, &export->name_length);
				if (status != ferr_ok) {
					goto out;
				}

				export->address = stub_override->new_address;
				export->flags = 0;
				export->image = image;
				export->reexport_source = NULL;

				goto out;
			}
		}
	}

	dymple_log_debug(dymple_log_category_resolution, "Resolving %.*s in %.*s\n", (int)name_length, name, (int)image->name_length, image->name);

	status = dymple_export_trie_find(image, name, name_length, &export_info);
	if (status != ferr_ok) {
		// try to see if we can resolve it in a reexported dylib
		for (size_t i = 0; i < image->reexport_count; ++i) {
			if (dymple_resolve_export(image->reexports[i], name, name_length, &export) == ferr_ok) {
				status = ferr_ok;
				break;
			}
		}

		goto out;
	}

	status = dymple_leb128_decode_unsigned(&export_info[offset], SIZE_MAX, &raw_flags, &uleb_size);
	if (status != ferr_ok) {
		goto out;
	}
	offset += uleb_size;

	flags = macho_export_flags_get(raw_flags);
	kind = macho_export_flags_get_kind(raw_flags);

	dymple_log_debug(dymple_log_category_resolution, "Resolved symbol %.*s with flags=%x and kind=%d\n", (int)name_length, name, flags, kind);

	if (flags & ~macho_export_symbol_flag_reexport) {
		dymple_log_error(dymple_log_category_resolution, "Unsupported Mach-O export symbol flag value: %u\n", flags);
		status = ferr_unsupported;
		goto out;
	}

	if (flags & macho_export_symbol_flag_reexport) {
		uintmax_t library_ordinal = 0;
		const char* original_name = NULL;
		size_t original_name_length = 0;
		dymple_symbol_t* reexport = NULL;
		dymple_image_t* library = NULL;

		status = dymple_leb128_decode_unsigned(&export_info[offset], SIZE_MAX, &library_ordinal, &uleb_size);
		if (status != ferr_ok) {
			goto out;
		}
		offset += uleb_size;

		original_name = &export_info[offset];
		original_name_length = simple_strlen(original_name);

		if (original_name_length == 0) {
			original_name = name;
			original_name_length = name_length;
		}

		if (library_ordinal > 0 && library_ordinal <= image->dependency_count) {
			library = image->dependencies[library_ordinal - 1];
		} else {
			dymple_log_error(dymple_log_category_resolution, "Invalid library ordinal\n");
			sys_abort();
		}

		status = dymple_resolve_export(library, original_name, original_name_length, &reexport);
		if (status != ferr_ok) {
			goto out;
		}

		status = simple_ghmap_lookup(&image->exports_table, name, name_length, true, SIZE_MAX, &created, (void*)&export, NULL);
		if (status != ferr_ok) {
			goto out;
		}

		if (!created) {
			goto out;
		}

		status = simple_ghmap_lookup_stored_key(&image->exports_table, name, name_length, (void*)&export->name, &export->name_length);
		if (status != ferr_ok) {
			goto out;
		}

		export->address = NULL;
		export->flags = 0;
		export->image = image;
		export->reexport_source = reexport;
		goto out;
	}

	status = dymple_leb128_decode_unsigned(&export_info[offset], SIZE_MAX, &value, &uleb_size);
	if (status != ferr_ok) {
		goto out;
	}
	offset += uleb_size;

	dymple_log_debug(dymple_log_category_resolution, "Resolved symbol %.*s with value=%lx\n", (int)name_length, name, value);

	if (kind != macho_export_symbol_kind_regular) {
		dymple_log_error(dymple_log_category_resolution, "Unsupported Mach-O export symbol kind value: %u\n", kind);
		status = ferr_unsupported;
		goto out;
	}

	// find the symbol's address
	for (size_t i = 0; i < image->section_count; ++i) {
		const dymple_section_t* section = &image->sections[i];

		if (section->memory_offset <= value && section->memory_offset + section->size > value) {
			// this is the section that contains the symbol
			address = (void*)((uintptr_t)section->address + (value - section->memory_offset));
			break;
		}
	}

	if (!address) {
		// *super* weird
		status = ferr_no_such_resource;
		dymple_log_debug(dymple_log_category_resolution, "Failed to resolve symbol address for symbol %.*s after finding export info (this shouldn't happen)\n", (int)name_length, name);
		goto out;
	}

	status = simple_ghmap_lookup(&image->exports_table, name, name_length, true, SIZE_MAX, &created, (void*)&export, NULL);
	if (status != ferr_ok) {
		goto out;
	}

	if (!created) {
		goto out;
	}

	status = simple_ghmap_lookup_stored_key(&image->exports_table, name, name_length, (void*)&export->name, &export->name_length);
	if (status != ferr_ok) {
		goto out;
	}

	export->address = address;
	export->flags = 0;
	export->image = image;
	export->reexport_source = NULL;

out:
	if (status == ferr_ok) {
		if (out_export) {
			*out_export = export;
		}
	}
	return status;
};

ferr_t dymple_resolve_symbol(dymple_image_t* image, const char* symbol_name, bool search_dependencies, void** out_address) {
	return dymple_resolve_symbol_n(image, symbol_name, symbol_name ? simple_strlen(symbol_name) : 0, search_dependencies, out_address);
};

ferr_t dymple_resolve_symbol_n(dymple_image_t* image, const char* symbol_name, size_t symbol_name_length, bool search_dependencies, void** out_address) {
	ferr_t status = ferr_ok;
	dymple_symbol_t* symbol = NULL;

	if (!symbol_name) {
		status = ferr_invalid_argument;
		goto out;
	}

	// we assume a two-level namespace is being used;
	// TODO: support flat namespace

	dymple_api_lock();

	status = dymple_resolve_export(image, symbol_name, symbol_name_length, &symbol);
	if (search_dependencies && status == ferr_no_such_resource) {
		for (size_t i = 0; i < image->dependency_count; ++i) {
			status = dymple_resolve_export(image->dependencies[i], symbol_name, symbol_name_length, &symbol);
			if (status == ferr_ok) {
				break;
			}
		}
	}

	dymple_api_unlock();

	if (status != ferr_ok) {
		goto out;
	}

out:
	if (status == ferr_ok) {
		if (out_address) {
			*out_address = dymple_symbol_address(symbol);
		}
	}
	return status;
};
