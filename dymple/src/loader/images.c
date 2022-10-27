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

#include <dymple/images.private.h>
#include <dymple/log.h>
#include <dymple/util.h>
#include <dymple/relocations.h>
#include <dymple/api.private.h>
#include <libsimple/libsimple.h>

#include <libmacho/libmacho.h>

// FIXME: we should not be special-casing library paths
#define LIBDYMPLE_PATH "/sys/lib/libdymple.dylib"

static simple_ghmap_t images;

// TODO: this should be a mutex
// TODO: actually use this lol
static sys_spinlock_t images_lock = SYS_SPINLOCK_INIT;

static ferr_t dymple_open_image_by_name(const char* name, size_t name_length, sys_file_t** out_file);
static ferr_t dymple_load_image_from_file_internal(sys_file_t* file, dymple_image_t** out_image);
static ferr_t dymple_load_image_by_name_n_internal(const char* name, size_t name_length, dymple_image_t** out_image);

#if 0
static bool dymple_defined_symbols_print_each(void* context, simple_ghmap_t* hashmap, simple_ghmap_hash_t hash, const void* key, size_t key_size, void* entry, size_t entry_size) {
	dymple_symbol_t* symbol = entry;

	dymple_log_debug(dymple_log_category_image_loading, "\tSymbol \"%.*s\" loaded at %p (flags = %u)\n", (int)key_size, (const char*)key, symbol->address, symbol->flags);

	return true;
};

static bool dymple_undefined_symbols_print_each(void* context, simple_ghmap_t* hashmap, simple_ghmap_hash_t hash, const void* key, size_t key_size, void* entry, size_t entry_size) {
	dymple_symbol_t* symbol = entry;
	const char* file_name = NULL;
	size_t file_name_length = 0;

	if (symbol->image) {
		file_name = symbol->image->name;
		file_name_length = symbol->image->name_length;
	}

	dymple_log_debug(dymple_log_category_image_loading, "\tSymbol \"%.*s\" (flags = %u) must be bound%s%.*s%s\n", (int)key_size, (const char*)key, symbol->flags, file_name ? " (expected in \"" : "", (int)file_name_length, file_name ? file_name : "", file_name ? "\")" : "");

	return true;
};
#endif

static bool dymple_images_print_each(void* context, simple_ghmap_t* hashmap, simple_ghmap_hash_t hash, const void* key, size_t key_size, void* entry, size_t entry_size) {
	dymple_image_t* image = entry;

	dymple_log_debug(dymple_log_category_image_loading, "Image \"%.*s\" loaded at %p\n", (int)image->name_length, (const char*)image->name, image->base);

#if 0
	dymple_log_debug(dymple_log_category_image_loading, "Defined symbols:\n");
	simple_ghmap_for_each(&image->defined_symbol_table, dymple_defined_symbols_print_each, NULL);

	dymple_log_debug(dymple_log_category_image_loading, "Undefined symbols:\n");
	simple_ghmap_for_each(&image->undefined_symbol_table, dymple_undefined_symbols_print_each, NULL);
#endif

	if (image->entry_address) {
		dymple_log_debug(dymple_log_category_image_loading, "Image has an entry point at %p\n", image->entry_address);
	}

	return true;
};

ferr_t dymple_images_init(dymple_image_t** out_image) {
	ferr_t status = ferr_ok;
	sys_file_t* binary_file = NULL;

	status = simple_ghmap_init_string_to_generic(&images, 4, sizeof(dymple_image_t), simple_ghmap_allocate_sys_mempool, simple_ghmap_free_sys_mempool, NULL);
	if (status != ferr_ok) {
		goto out;
	}

	status = sys_file_open_special(sys_file_special_id_process_binary, &binary_file);
	if (status != ferr_ok) {
		goto out;
	}

	status = dymple_load_image_from_file(binary_file, out_image);
	if (status != ferr_ok) {
		goto out;
	}

	if (dymple_log_is_enabled(dymple_log_type_debug, dymple_log_category_image_loading)) {
		simple_ghmap_for_each(&images, dymple_images_print_each, NULL);
	}

out:
	if (binary_file) {
		sys_release(binary_file);
	}
	return status;
};

static ferr_t dymple_read_exact(sys_file_t* file, size_t offset, void* buffer, size_t buffer_size) {
	size_t read = 0;
	ferr_t status = sys_file_read_retry(file, offset, buffer_size, buffer, &read);
	if (status != ferr_ok) {
		goto out;
	}

	if (read != buffer_size) {
		status = ferr_invalid_argument;
		goto out;
	}

out:
	return status;
};

static ferr_t dymple_load_image_internal(sys_file_t* file, const char* file_path, size_t file_path_length, dymple_image_t** out_image) {
	ferr_t status = ferr_ok;
	macho_header_t header = {0};
	bool release_file_on_fail = false;
	dymple_image_t* image = NULL;
	void* file_load_top = NULL;
	bool created = false;
	bool destroy_symbol_table_on_fail = false;
	bool destroy_undefined_symbol_table_on_fail = false;
	bool destroy_exports_table_on_fail = false;
	bool destroy_imports_table_on_fail = false;

	macho_load_command_t load_command = {0};
	size_t file_offset = 0;
	size_t section_index = 0;
	size_t segment_index = 0;

	size_t entry_point_file_offset = SIZE_MAX;
	dymple_relocation_info_t relocation_info = {0};

	status = simple_ghmap_lookup(&images, file_path, file_path_length, true, SIZE_MAX, &created, (void*)&image, NULL);
	if (status != ferr_ok) {
		goto out;
	}

	dymple_log_debug(dymple_log_category_image_loading, "Going to load %.*s\n", (int)file_path_length, file_path);

	if (!created) {
		// if we already have it loaded, return success, but don't do anything else.
		dymple_log_debug(dymple_log_category_image_loading, "Image already loaded.\n");
		status = ferr_ok;
		goto out;
	}

	simple_memset(image, 0, sizeof(*image));

	image->file_load_base = (void*)UINTPTR_MAX;
	image->is_libdymple = file_path_length == sizeof(LIBDYMPLE_PATH) - 1 && simple_strncmp(file_path, LIBDYMPLE_PATH, file_path_length) == 0;

	// use the stored hashmap key instead of copying the file path yet again.
	// this image is only valid for as long as its present in the hashmap anyways.
	status = simple_ghmap_lookup_stored_key(&images, file_path, file_path_length, (void*)&image->name, &image->name_length);
	if (status != ferr_ok) {
		goto out;
	}

	if (sys_retain(file) != ferr_ok) {
		status = ferr_permanent_outage;
		goto out;
	}
	release_file_on_fail = true;

	status = dymple_read_exact(file, 0, &header, sizeof(macho_header_t));
	if (status != ferr_ok) {
		goto out;
	}

	image->file = file;

	// determine how big our block of memory needs to be.
	// at the same time, determine how many sections we have.
	file_offset = sizeof(header);
	for (size_t i = 0; i < header.command_count; (++i), (file_offset += load_command.size)) {
		macho_load_command_segment_64_t segment_64_load_command = {0};

		status = dymple_read_exact(file, file_offset, &load_command, sizeof(load_command));
		if (status != ferr_ok) {
			goto out;
		}

		if (load_command.type != macho_load_command_type_segment_64) {
			continue;
		}

		status = dymple_read_exact(file, file_offset, &segment_64_load_command, sizeof(segment_64_load_command));
		if (status != ferr_ok) {
			goto out;
		}

		image->section_count += segment_64_load_command.section_count;
		++image->segment_count;

		if (segment_64_load_command.initial_memory_protection == 0 && segment_64_load_command.maximum_memory_protection == 0) {
			// if the memory protection is set to 0, this is a reserve-as-invalid segment.
			// in that case, we don't consider it for the memory allocation size.
			// this is most likely the __PAGEZERO segment.
			// XXX: this is wrong; the segment needs to actually be reserved to ensure that no memory from the region is ever allocated.
			continue;
		}

		if (segment_64_load_command.memory_address < (uintptr_t)image->file_load_base) {
			image->file_load_base = (void*)segment_64_load_command.memory_address;
		}

		if (segment_64_load_command.memory_address + segment_64_load_command.memory_size > (uintptr_t)file_load_top) {
			file_load_top = (void*)(segment_64_load_command.memory_address + segment_64_load_command.memory_size);
		}
	}

	image->size = (uint64_t)(file_load_top - image->file_load_base);

	status = sys_page_allocate(sys_page_round_up_count(image->size), 0, &image->base);
	if (status != ferr_ok) {
		goto out;
	}

	if (sys_mempool_allocate(sizeof(dymple_section_t) * image->section_count, NULL, (void*)&image->sections) != ferr_ok) {
		status = ferr_temporary_outage;
		goto out;
	}

	simple_memset(image->sections, 0, sizeof(dymple_section_t) * image->section_count);

	if (sys_mempool_allocate(sizeof(dymple_segment_t) * image->segment_count, NULL, (void*)&image->segments) != ferr_ok) {
		status = ferr_temporary_outage;
		goto out;
	}

	simple_memset(image->segments, 0, sizeof(dymple_segment_t) * image->segment_count);

	// now load the segments.
	// also determine the entry offset.
	// also load the relocation info.
	file_offset = sizeof(header);
	for (size_t i = 0; i < header.command_count; (++i), (file_offset += load_command.size)) {
		status = dymple_read_exact(file, file_offset, &load_command, sizeof(load_command));
		if (status != ferr_ok) {
			goto out;
		}

		if (load_command.type == macho_load_command_type_segment_64) {
			macho_load_command_segment_64_t segment_64_load_command = {0};

			status = dymple_read_exact(file, file_offset, &segment_64_load_command, sizeof(segment_64_load_command));
			if (status != ferr_ok) {
				goto out;
			}

			if (segment_64_load_command.initial_memory_protection == 0 && segment_64_load_command.maximum_memory_protection == 0) {
				// skip loading reserved-as-invalid segments
				// XXX: this is wrong
				image->segments[segment_index].address = NULL;
			} else {
				void* this_load_base = (char*)image->base + (segment_64_load_command.memory_address - (uintptr_t)image->file_load_base);

				dymple_log_debug(dymple_log_category_image_loading, "Loading %llu bytes at %p (with a target size of %llu bytes; zeroing rest)\n", segment_64_load_command.file_size, this_load_base, segment_64_load_command.memory_size);

				status = dymple_read_exact(file, segment_64_load_command.file_offset, this_load_base, segment_64_load_command.file_size);
				if (status != ferr_ok) {
					goto out;
				}

				// zero out unused memory
				simple_memset((char*)this_load_base + segment_64_load_command.file_size, 0, segment_64_load_command.memory_size - segment_64_load_command.file_size);

				image->segments[segment_index].address = this_load_base;
			}

			image->segments[segment_index].size = segment_64_load_command.memory_size;
			simple_memcpy(image->segments[segment_index].name, segment_64_load_command.segment_name, simple_min(sizeof(image->segments[segment_index].name), sizeof(segment_64_load_command.segment_name)));
			++segment_index;

			// now load section information
			for (size_t j = 0; j < segment_64_load_command.section_count; ++j) {
				macho_section_64_t section_64 = {0};

				status = dymple_read_exact(file, file_offset + sizeof(segment_64_load_command) + (sizeof(section_64) * j), &section_64, sizeof(section_64));
				if (status != ferr_ok) {
					goto out;
				}

				if (segment_64_load_command.initial_memory_protection == 0 && segment_64_load_command.maximum_memory_protection == 0) {
					image->sections[section_index].address = NULL;
				} else {
					image->sections[section_index].address = (char*)image->base + (section_64.memory_address - (uintptr_t)image->file_load_base);
				}
				image->sections[section_index].size = section_64.size;
				simple_memcpy(image->sections[section_index].section_name, section_64.section_name, simple_min(sizeof(image->sections[section_index].section_name), sizeof(section_64.section_name)));
				simple_memcpy(image->sections[section_index].segment_name, section_64.segment_name, simple_min(sizeof(image->sections[section_index].segment_name), sizeof(section_64.segment_name)));
				image->sections[section_index].file_offset = section_64.file_offset;

				++section_index;
			}
		} else if (load_command.type == macho_load_command_type_entry_point) {
			macho_load_command_entry_point_t entry_point_load_command = {0};

			status = dymple_read_exact(file, file_offset, &entry_point_load_command, sizeof(entry_point_load_command));
			if (status != ferr_ok) {
				goto out;
			}

			entry_point_file_offset = entry_point_load_command.entry_offset;
		} else if (load_command.type == macho_load_command_type_compressed_dynamic_linker_info_only) {
			macho_load_command_compressed_dynamic_linker_info_t compressed_dynamic_linker_info_load_command = {0};

			status = dymple_read_exact(file, file_offset, &compressed_dynamic_linker_info_load_command, sizeof(compressed_dynamic_linker_info_load_command));
			if (status != ferr_ok) {
				goto out;
			}

			relocation_info.rebase_instructions_size = compressed_dynamic_linker_info_load_command.rebase_info_size;
			relocation_info.bind_instructions_size = compressed_dynamic_linker_info_load_command.bind_info_size;
			relocation_info.weak_bind_instructions_size = compressed_dynamic_linker_info_load_command.weak_bind_info_size;
			image->lazy_bind_instructions_size = compressed_dynamic_linker_info_load_command.lazy_bind_info_size;
			image->export_trie_size = compressed_dynamic_linker_info_load_command.export_info_size;

			if (sys_mempool_allocate(compressed_dynamic_linker_info_load_command.rebase_info_size, NULL, &relocation_info.rebase_instructions) != ferr_ok) {
				status = ferr_temporary_outage;
				goto out;
			}

			if (sys_mempool_allocate(compressed_dynamic_linker_info_load_command.bind_info_size, NULL, &relocation_info.bind_instructions) != ferr_ok) {
				status = ferr_temporary_outage;
				goto out;
			}

			if (sys_mempool_allocate(compressed_dynamic_linker_info_load_command.weak_bind_info_size, NULL, &relocation_info.weak_bind_instructions) != ferr_ok) {
				status = ferr_temporary_outage;
				goto out;
			}

			if (sys_mempool_allocate(compressed_dynamic_linker_info_load_command.lazy_bind_info_size, NULL, &image->lazy_bind_instructions) != ferr_ok) {
				status = ferr_temporary_outage;
				goto out;
			}

			if (sys_mempool_allocate(compressed_dynamic_linker_info_load_command.export_info_size, NULL, &image->export_trie) != ferr_ok) {
				status = ferr_temporary_outage;
				goto out;
			}

			status = dymple_read_exact(file, compressed_dynamic_linker_info_load_command.rebase_info_offset, relocation_info.rebase_instructions, relocation_info.rebase_instructions_size);
			if (status != ferr_ok) {
				goto out;
			}

			status = dymple_read_exact(file, compressed_dynamic_linker_info_load_command.bind_info_offset, relocation_info.bind_instructions, relocation_info.bind_instructions_size);
			if (status != ferr_ok) {
				goto out;
			}

			status = dymple_read_exact(file, compressed_dynamic_linker_info_load_command.weak_bind_info_offset, relocation_info.weak_bind_instructions, relocation_info.weak_bind_instructions_size);
			if (status != ferr_ok) {
				goto out;
			}

			status = dymple_read_exact(file, compressed_dynamic_linker_info_load_command.lazy_bind_info_offset, image->lazy_bind_instructions, image->lazy_bind_instructions_size);
			if (status != ferr_ok) {
				goto out;
			}

			status = dymple_read_exact(file, compressed_dynamic_linker_info_load_command.export_info_offset, image->export_trie, image->export_trie_size);
			if (status != ferr_ok) {
				goto out;
			}
		}
	}

	section_index = 0;

	dymple_log_debug(dymple_log_category_image_loading, "Image loaded into memory; looking for dependencies...\n");

	// now look for dylib loading commands
	file_offset = sizeof(header);
	for (size_t i = 0; i < header.command_count; (++i), (file_offset += load_command.size)) {
		macho_load_command_dylib_t dylib_load_command = {0};
		void* this_load_base = NULL;
		char* load_path = NULL;
		size_t load_path_length = 0;
		sys_file_t* dep_file = NULL;
		dymple_image_t* dep_image = NULL;

		status = dymple_read_exact(file, file_offset, &load_command, sizeof(load_command));
		if (status != ferr_ok) {
			goto out;
		}

		if (load_command.type != macho_load_command_type_load_dylib) {
			continue;
		}

		status = dymple_read_exact(file, file_offset, &dylib_load_command, sizeof(dylib_load_command));
		if (status != ferr_ok) {
			goto out;
		}

		load_path_length = load_command.size - dylib_load_command.name_offset;

		if (sys_mempool_allocate(load_path_length, NULL, (void*)&load_path) != ferr_ok) {
			status = ferr_temporary_outage;
			goto out;
		}

		status = dymple_read_exact(file, file_offset + dylib_load_command.name_offset, load_path, load_path_length);
		if (status != ferr_ok) {
			dymple_abort_status(sys_mempool_free(load_path));
			goto out;
		}

		// the name can include zero padding at the end, so find the real length
		load_path_length = simple_strnlen(load_path, load_path_length);

		status = dymple_load_image_by_name_n_internal(load_path, load_path_length, &dep_image);

		dymple_abort_status(sys_mempool_free(load_path));

		if (status != ferr_ok) {
			goto out;
		}

		// register the new image as a dependency of our image
		if (sys_mempool_reallocate(image->dependencies, sizeof(*image->dependencies) * (image->dependency_count + 1), NULL, (void*)&image->dependencies) != ferr_ok) {
			status = ferr_temporary_outage;
			goto out;
		}

		image->dependencies[image->dependency_count] = dep_image;
		++image->dependency_count;

		// register our image as a dependent of the new image
		if (sys_mempool_reallocate(dep_image->dependents, sizeof(*dep_image->dependents) * (dep_image->dependent_count + 1), NULL, (void*)&dep_image->dependents) != ferr_ok) {
			status = ferr_temporary_outage;
			goto out;
		}

		dep_image->dependents[dep_image->dependent_count] = image;
		++dep_image->dependent_count;
	}

	dymple_log_debug(dymple_log_category_image_loading, "Loaded image dependencies (%zu dylib(s)); now looking for symbols\n", image->dependency_count);

#if 0
	if (simple_ghmap_init_string_to_generic(&image->defined_symbol_table, 16, sizeof(dymple_symbol_t), simple_ghmap_allocate_sys_mempool, simple_ghmap_free_sys_mempool, NULL) != ferr_ok) {
		status = ferr_temporary_outage;
		goto out;
	}
	destroy_symbol_table_on_fail = true;

	if (simple_ghmap_init_string_to_generic(&image->undefined_symbol_table, 4, sizeof(dymple_symbol_t), simple_ghmap_allocate_sys_mempool, simple_ghmap_free_sys_mempool, NULL) != ferr_ok) {
		status = ferr_temporary_outage;
		goto out;
	}
	destroy_undefined_symbol_table_on_fail = true;
#endif

	if (simple_ghmap_init_string_to_generic(&image->exports_table, 4, sizeof(dymple_symbol_t), simple_ghmap_allocate_sys_mempool, simple_ghmap_free_sys_mempool, NULL) != ferr_ok) {
		status = ferr_temporary_outage;
		goto out;
	}
	destroy_exports_table_on_fail = true;

#if 0
	if (simple_ghmap_init_string_to_generic(&image->imports_table, 4, sizeof(dymple_import_t), simple_ghmap_allocate_sys_mempool, simple_ghmap_free_sys_mempool, NULL) != ferr_ok) {
		status = ferr_temporary_outage;
		goto out;
	}
	destroy_imports_table_on_fail = true;
#endif

	// now let's fill up the symbol table
	file_offset = sizeof(header);
	for (size_t i = 0; i < header.command_count; (++i), (file_offset += load_command.size)) {
		macho_load_command_symbol_table_info_t symbol_table_info_load_command = {0};
		char* string_table = NULL;

		status = dymple_read_exact(file, file_offset, &load_command, sizeof(load_command));
		if (status != ferr_ok) {
			goto out;
		}

		if (load_command.type != macho_load_command_type_symbol_table_info) {
			continue;
		}

		status = dymple_read_exact(file, file_offset, &symbol_table_info_load_command, sizeof(symbol_table_info_load_command));
		if (status != ferr_ok) {
			goto out;
		}

		dymple_log_debug(dymple_log_category_image_loading, "Image contains %u symbols (with a string table of size %u)\n", symbol_table_info_load_command.symbol_table_entry_count, symbol_table_info_load_command.string_table_size);

		// TODO: we *really* need the ability to memory-map files
		if (sys_mempool_allocate(symbol_table_info_load_command.string_table_size, NULL, (void*)&string_table) != ferr_ok) {
			status = ferr_temporary_outage;
			goto out;
		}

		dymple_log_debug(dymple_log_category_image_loading, "Allocated image string table\n");

		status = dymple_read_exact(file, symbol_table_info_load_command.string_table_offset, string_table, symbol_table_info_load_command.string_table_size);
		if (status != ferr_ok) {
			goto out;
		}

		dymple_log_debug(dymple_log_category_image_loading, "Loaded image string table\n");

		for (size_t j = 0; j < symbol_table_info_load_command.symbol_table_entry_count; ++j) {
			macho_symbol_table_entry_t entry = {0};
			bool created = false;
			dymple_symbol_t* symbol = NULL;

			status = dymple_read_exact(file, symbol_table_info_load_command.symbol_table_offset + (sizeof(macho_symbol_table_entry_t) * j), &entry, sizeof(entry));
			if (status != ferr_ok) {
				dymple_abort_status(sys_mempool_free(string_table));
				goto out;
			}

#if 0
			switch (macho_symbol_table_entry_get_type(entry.type)) {
				case macho_symbol_table_entry_type_section: {
					if (!macho_symbol_table_entry_is_external(entry.type)) {
						continue;
					}

					if (macho_symbol_table_entry_is_private_extern(entry.type)) {
						continue;
					}

					if (simple_ghmap_lookup(&image->defined_symbol_table, &string_table[entry.string_table_name_offset], SIZE_MAX, true, sizeof(dymple_symbol_t), &created, (void*)&symbol, NULL) != ferr_ok) {
						status = ferr_temporary_outage;
						goto out;
					}

					if (!created) {
						continue;
					}

					symbol->address = image->sections[entry.section].address + entry.value;
					symbol->image = image;
				} break;

				case macho_symbol_table_entry_type_undefined: {
					uint8_t library_index = 0;

					if (simple_ghmap_lookup(&image->undefined_symbol_table, &string_table[entry.string_table_name_offset], SIZE_MAX, true, sizeof(dymple_symbol_t), &created, (void*)&symbol, NULL) != ferr_ok) {
						status = ferr_temporary_outage;
						goto out;
					}

					if (!created) {
						continue;
					}

					library_index = macho_symbol_table_entry_library_index(entry.description);

					symbol->address = NULL;

					if (macho_symbol_table_entry_library_index_is_special(library_index)) {
						symbol->image = NULL;
					} else {
						symbol->image = image->dependencies[library_index - 1];
					}
				} break;
			}
#endif
		}

		dymple_abort_status(sys_mempool_free(string_table));

		break;
	}

	// determine the image's entry point address (if it has one)
	if (entry_point_file_offset != SIZE_MAX) {
		for (size_t i = 0; i < image->section_count; ++i) {
			dymple_section_t* section = &image->sections[i];

			if (section->file_offset <= entry_point_file_offset && section->file_offset + section->size > entry_point_file_offset) {
				image->entry_address = (char*)section->address + (entry_point_file_offset - section->file_offset);
				break;
			}
		}
	}

	// now perform relocations
	status = dymple_relocate_image(image, &relocation_info);
	if (status != ferr_ok) {
		goto out;
	}

out:
	if (relocation_info.rebase_instructions) {
		sys_abort_status(sys_mempool_free(relocation_info.rebase_instructions));
	}
	if (relocation_info.bind_instructions) {
		sys_abort_status(sys_mempool_free(relocation_info.bind_instructions));
	}
	if (relocation_info.weak_bind_instructions) {
		sys_abort_status(sys_mempool_free(relocation_info.weak_bind_instructions));
	}
	if (status == ferr_ok) {
		if (out_image) {
			*out_image = image;
		}
		if (created) {
			dymple_log_debug(dymple_log_category_image_load_address, "Image \"%.*s\" loaded at %p\n", (int)image->name_length, (const char*)image->name, image->base);
		}
	} else {
		if (image && created) {
			if (image->lazy_bind_instructions) {
				sys_abort_status(sys_mempool_free(image->lazy_bind_instructions));
			}
			if (image->export_trie) {
				sys_abort_status(sys_mempool_free(image->export_trie));
			}
			if (image->segments) {
				dymple_abort_status(sys_mempool_free(image->segments));
			}
			if (image->sections) {
				dymple_abort_status(sys_mempool_free(image->sections));
			}
#if 0
			if (destroy_imports_table_on_fail) {
				simple_ghmap_destroy(&image->imports_table);
			}
#endif
			if (destroy_exports_table_on_fail) {
				simple_ghmap_destroy(&image->exports_table);
			}
#if 0
			if (destroy_undefined_symbol_table_on_fail) {
				simple_ghmap_destroy(&image->undefined_symbol_table);
			}
			if (destroy_symbol_table_on_fail) {
				simple_ghmap_destroy(&image->defined_symbol_table);
			}
#endif
			if (image->base) {
				dymple_abort_status(sys_page_free(image->base));
			}
			if (image->dependencies) {
				dymple_abort_status(sys_mempool_free(image->dependencies));
			}
			if (image->dependents) {
				dymple_abort_status(sys_mempool_free(image->dependents));
			}
			dymple_abort_status(simple_ghmap_clear(&images, file_path, file_path_length));
		}
		if (release_file_on_fail) {
			sys_release(file);
		}
	}
	return status;
};

ferr_t dymple_load_image_by_name(const char* name, dymple_image_t** out_image) {
	return dymple_load_image_by_name_n(name, simple_strlen(name), out_image);
};

static ferr_t dymple_open_image_by_name(const char* name, size_t name_length, sys_file_t** out_file) {
	sys_file_t* file = NULL;
	ferr_t status = ferr_no_such_resource;

	if (!name || !out_file) {
		status = ferr_invalid_argument;
		goto out;
	}

	// TODO: support RPATH resolution

	status = sys_file_open_n(name, name_length, &file);

out:
	if (status == ferr_ok) {
		*out_file = file;
	} else {
		if (file) {
			sys_release(file);
		}
	}
	return status;
};

static ferr_t dymple_load_image_from_file_internal(sys_file_t* file, dymple_image_t** out_image) {
	ferr_t status = ferr_ok;
	char* file_path = NULL;
	size_t file_path_length = 0;

	status = sys_file_copy_path_allocate(file, &file_path, &file_path_length);
	if (status != ferr_ok) {
		goto out;
	}

	status = dymple_load_image_internal(file, file_path, file_path_length, out_image);

out:
	if (file_path) {
		dymple_abort_status(sys_mempool_free(file_path));
	}
	return status;
};

ferr_t dymple_load_image_from_file(sys_file_t* file, dymple_image_t** out_image) {
	dymple_api_lock();
	ferr_t status = dymple_load_image_from_file_internal(file, out_image);
	dymple_api_unlock();
	return status;
};

static ferr_t dymple_load_image_by_name_n_internal(const char* name, size_t name_length, dymple_image_t** out_image) {
	sys_file_t* file = NULL;
	ferr_t status = ferr_ok;

	status = dymple_open_image_by_name(name, name_length, &file);
	if (status != ferr_ok) {
		goto out;
	}

	status = dymple_load_image_from_file_internal(file, out_image);

out:
	if (file) {
		sys_release(file);
	}
	return status;
};

ferr_t dymple_load_image_by_name_n(const char* name, size_t name_length, dymple_image_t** out_image) {
	dymple_api_lock();
	ferr_t status = dymple_load_image_by_name_n_internal(name, name_length, out_image);
	dymple_api_unlock();
	return status;
};

DYMPLE_STRUCT(dymple_image_containing_address_context) {
	void* address;
	dymple_image_t* image;
};

static bool dymple_image_containing_address_iterator(void* _context, simple_ghmap_t* hashmap, simple_ghmap_hash_t hash, const void* key, size_t key_size, void* entry, size_t entry_size) {
	dymple_image_t* image = entry;
	dymple_image_containing_address_context_t* context = _context;

	if (image->base <= context->address && (uintptr_t)image->base + image->size > (uintptr_t)context->address) {
		context->image = image;
		return false;
	}

	return true;
};

dymple_image_t* dymple_image_containing_address(void* address) {
	dymple_image_containing_address_context_t context = {
		.address = address,
		.image = NULL,
	};

	simple_ghmap_for_each(&images, dymple_image_containing_address_iterator, &context);

	return context.image;
};

DYMPLE_STRUCT(dymple_find_loaded_image_by_name_n_context) {
	const char* name;
	size_t name_length;
	dymple_image_t* image;
};

static bool dymple_find_loaded_image_by_name_n_iterator(void* _context, simple_ghmap_t* hashmap, simple_ghmap_hash_t hash, const void* key, size_t key_size, void* entry, size_t entry_size) {
	dymple_image_t* image = entry;
	dymple_find_loaded_image_by_name_n_context_t* context = _context;

	if (context->name_length == image->name_length && simple_strncmp(context->name, image->name, context->name_length) == 0) {
		context->image = image;
		return false;
	}

	return true;
};

ferr_t dymple_find_loaded_image_by_name(const char* name, dymple_image_t** out_image) {
	return dymple_find_loaded_image_by_name_n(name, simple_strlen(name), out_image);
};

ferr_t dymple_find_loaded_image_by_name_n(const char* name, size_t name_length, dymple_image_t** out_image) {
	ferr_t status = ferr_ok;
	dymple_find_loaded_image_by_name_n_context_t context = {
		.name = name,
		.name_length = name_length,
		.image = NULL,
	};

	dymple_api_lock();

	simple_ghmap_for_each(&images, dymple_find_loaded_image_by_name_n_iterator, &context);

	dymple_api_unlock();

	if (!context.image) {
		status = ferr_no_such_resource;
		goto out;
	}

out:
	if (out_image) {
		*out_image = context.image;
	}
	return status;
};
