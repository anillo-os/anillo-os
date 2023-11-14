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

#ifndef _DYMPLE_IMAGES_PRIVATE_H_
#define _DYMPLE_IMAGES_PRIVATE_H_

#include <dymple/images.h>
#include <libsimple/ghmap.h>

DYMPLE_DECLARATIONS_BEGIN;

DYMPLE_STRUCT(dymple_symbol) {
	const char* name;
	size_t name_length;
	void* address;
	dymple_image_t* image;
	uint8_t flags;
	dymple_symbol_t* reexport_source;
};

DYMPLE_STRUCT(dymple_section) {
	char section_name[16];
	char segment_name[16];
	void* address;
	size_t size;
	size_t file_offset;
	size_t memory_offset;
};

DYMPLE_STRUCT(dymple_segment) {
	char name[16];
	void* address;
	size_t size;
};

DYMPLE_STRUCT(dymple_image) {
	const char* name;
	size_t name_length;

	sys_file_t* file;
	void* entry_address;

	void* base;
	void* file_load_base;
	size_t size;

	dymple_section_t* sections;
	size_t section_count;

	dymple_segment_t* segments;
	size_t segment_count;

	simple_ghmap_t exports_table;
	//simple_ghmap_t imports_table;

	size_t dependency_count;
	dymple_image_t** dependencies;

	size_t dependent_count;
	dymple_image_t** dependents;

	size_t reexport_count;
	dymple_image_t** reexports;

	void* export_trie;
	size_t export_trie_size;

	void* lazy_bind_instructions;
	size_t lazy_bind_instructions_size;

	bool is_libdymple;
};

DYMPLE_ALWAYS_INLINE void* dymple_symbol_address(const dymple_symbol_t* symbol) {
	while (symbol) {
		if (symbol->address) {
			return symbol->address;
		}
		symbol = symbol->reexport_source;
	}
	return NULL;
};

typedef void (*dymple_entry_point_f)(void);

ferr_t dymple_images_init(dymple_image_t** out_image);

dymple_image_t* dymple_image_containing_address(void* address);

DYMPLE_DECLARATIONS_END;

#endif // _DYMPLE_IMAGES_PRIVATE_H_
