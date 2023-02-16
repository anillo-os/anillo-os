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

#ifndef _LIBMACHO_LIBMACHO_H_
#define _LIBMACHO_LIBMACHO_H_

#include <stdint.h>
#include <stdbool.h>

#include <libmacho/base.h>

LIBMACHO_DECLARATIONS_BEGIN;

LIBMACHO_ENUM(int, macho_cpu_type) {
	macho_cpu_type_x86_64  = 0x01000007,
	macho_cpu_type_aarch64 = 0x0100000c,
};

LIBMACHO_ENUM(int, macho_cpu_subtype) {
	macho_cpu_subtype_x86_64_all  = 3,
	macho_cpu_subtype_aarch64_all = 0,
};

LIBMACHO_ENUM(uint32_t, macho_file_type) {
	macho_file_type_object = 1,
	macho_file_type_exectuable = 2,
	macho_file_type_dynamic_library = 6,
	macho_file_type_dynamic_linker = 7,
};

LIBMACHO_OPTIONS(uint32_t, macho_header_flags) {
	macho_header_flag_no_undefined_symbols    = 1ULL << 0,
	macho_header_flag_dynamically_linked      = 1ULL << 2,
	macho_header_flag_use_two_level_namespace = 1ULL << 7,
	macho_header_flag_pie                     = 1ULL << 21,
};

LIBMACHO_OPTIONS(int, macho_memory_protection_flags) {
	macho_memory_protection_flag_read    = 1ULL << 0,
	macho_memory_protection_flag_write   = 1ULL << 1,
	macho_memory_protection_flag_execute = 1ULL << 2,
};

LIBMACHO_PACKED_STRUCT(macho_header) {
	uint32_t magic;
	macho_cpu_type_t cpu_type;
	macho_cpu_subtype_t cpu_subtype;
	macho_file_type_t file_type;
	uint32_t command_count;
	uint32_t total_command_size;
	macho_header_flags_t flags;
	uint32_t reserved;
	char load_commands[];
};

#define MACHO_MAGIC_64 (0xfeedfacfU)

LIBMACHO_ENUM(uint32_t, macho_load_command_type) {
	macho_load_command_type_symbol_table_info                   = 0x02,
	macho_load_command_type_unix_thread                         = 0x05,
	macho_load_command_type_dynamic_symbol_table_info           = 0x0b,
	macho_load_command_type_load_dylib                          = 0x0c,
	macho_load_command_type_load_dynamic_linker                 = 0x0e,
	macho_load_command_type_segment_64                          = 0x19,
	macho_load_command_type_reexport_dylib                      = 0x8000001f,
	macho_load_command_type_compressed_dynamic_linker_info_only = 0x80000022,
	macho_load_command_type_entry_point                         = 0x80000028,
};

LIBMACHO_PACKED_STRUCT(macho_load_command) {
	macho_load_command_type_t type;
	uint32_t size;
};

LIBMACHO_PACKED_STRUCT(macho_load_command_segment_64) {
	macho_load_command_t base;
	char segment_name[16];
	uint64_t memory_address;
	uint64_t memory_size;
	uint64_t file_offset;
	uint64_t file_size;
	int maximum_memory_protection;
	int initial_memory_protection;
	uint32_t section_count;
	uint32_t flags;
};

LIBMACHO_PACKED_STRUCT(macho_section_64) {
	char section_name[16];
	char segment_name[16];
	uint64_t memory_address;
	uint64_t size;
	uint32_t file_offset;
	uint32_t alignment;
	uint32_t relocations_file_offset;
	uint32_t relocation_count;
	uint32_t flags;
	uint32_t reserved1;
	uint32_t reserved2;
	uint32_t reserved3;
};

LIBMACHO_PACKED_STRUCT(macho_load_command_thread) {
	macho_load_command_t base;
};

LIBMACHO_PACKED_STRUCT(macho_load_command_dynamic_linker) {
	macho_load_command_t base;

	/**
	 * The offset of the name *within the command*. This is NOT relative to the start of the file; it's relative to the start of this load command.
	 */
	uint32_t name_offset;
};

LIBMACHO_PACKED_STRUCT(macho_load_command_dylib) {
	macho_load_command_t base;

	/**
	 * The offset of the name *within the command*. This is NOT relative to the start of the file; it's relative to the start of this load command.
	 */
	uint32_t name_offset;

	uint32_t timestamp;
	uint32_t current_version;
	uint32_t compat_version;
};

LIBMACHO_PACKED_STRUCT(macho_load_command_symbol_table_info) {
	macho_load_command_t base;

	uint32_t symbol_table_offset;
	uint32_t symbol_table_entry_count;

	uint32_t string_table_offset;
	uint32_t string_table_size;
};

LIBMACHO_PACKED_STRUCT(macho_load_command_dynamic_symbol_table_info) {
	macho_load_command_t base;

	uint32_t local_symbols_start_index;
	uint32_t local_symbol_count;

	uint32_t external_symbols_start_index;
	uint32_t external_symbol_count;

	uint32_t undefined_symbols_start_index;
	uint32_t undefined_symbol_count;

	uint32_t table_of_contents_offset;
	uint32_t table_of_contents_entry_count;

	uint32_t module_table_offset;
	uint32_t module_table_entry_count;

	uint32_t external_reference_table_offset;
	uint32_t external_reference_table_entry_count;

	uint32_t indirect_symbol_table_offset;
	uint32_t indirect_symbol_table_entry_count;

	uint32_t external_relocations_offset;
	uint32_t external_relocation_count;

	uint32_t local_relocations_offset;
	uint32_t local_relocation_count;
};

LIBMACHO_ENUM(uint8_t, macho_symbol_table_entry_type) {
	macho_symbol_table_entry_type_undefined = 0,
	macho_symbol_table_entry_type_absolute  = 1,
	macho_symbol_table_entry_type_indirect  = 5,
	macho_symbol_table_entry_type_prebound  = 6,
	macho_symbol_table_entry_type_section   = 7,
};

LIBMACHO_ENUM(uint8_t, macho_symbol_table_entry_section) {
	macho_symbol_table_entry_section_none = 0,
	// all other values are valid section indicies
};

LIBMACHO_ENUM(uint8_t, macho_symbol_table_entry_library_index) {
	macho_symbol_table_entry_library_index_self           = 0,
	macho_symbol_table_entry_library_index_dynamic_lookup = 0xfe,
	macho_symbol_table_entry_library_index_executable     = 0xff,
};

LIBMACHO_PACKED_STRUCT(macho_symbol_table_entry) {
	uint32_t string_table_name_offset;
	uint8_t type;
	uint8_t section;
	uint16_t description;
	uint64_t value;
};

LIBMACHO_ALWAYS_INLINE macho_symbol_table_entry_type_t macho_symbol_table_entry_get_type(uint8_t type_field) {
	return (type_field >> 1) & 7;
};

LIBMACHO_ALWAYS_INLINE bool macho_symbol_table_entry_is_external(uint8_t type_field) {
	return type_field & 1;
};

LIBMACHO_ALWAYS_INLINE bool macho_symbol_table_entry_is_private_extern(uint8_t type_field) {
	return (type_field >> 4) & 1;
};

LIBMACHO_ALWAYS_INLINE uint8_t macho_symbol_table_entry_library_index(uint16_t description_field) {
	return description_field >> 8;
};

LIBMACHO_ALWAYS_INLINE bool macho_symbol_table_entry_library_index_is_special(uint8_t library_index) {
	switch (library_index) {
		case macho_symbol_table_entry_library_index_self:
		case macho_symbol_table_entry_library_index_dynamic_lookup:
		case macho_symbol_table_entry_library_index_executable:
			return true;
		default:
			return false;
	}
};

LIBMACHO_PACKED_STRUCT(macho_load_command_entry_point) {
	macho_load_command_t base;

	uint64_t entry_offset;
	uint64_t stack_size;
};

LIBMACHO_PACKED_STRUCT(macho_load_command_compressed_dynamic_linker_info) {
	macho_load_command_t base;

	uint32_t rebase_info_offset;
	uint32_t rebase_info_size;

	uint32_t bind_info_offset;
	uint32_t bind_info_size;

	uint32_t weak_bind_info_offset;
	uint32_t weak_bind_info_size;

	uint32_t lazy_bind_info_offset;
	uint32_t lazy_bind_info_size;

	uint32_t export_info_offset;
	uint32_t export_info_size;
};

LIBMACHO_ENUM(uint8_t, macho_relocation_type) {
	macho_relocation_type_pointer = 1,
	macho_relocation_type_text_absolute_32 = 2,
	macho_relocation_type_text_pc_relative_32 = 3,
};

LIBMACHO_ENUM(uint8_t, macho_rebase_opcode) {
	macho_rebase_opcode_done = 0,
	macho_rebase_opcode_set_type_immediate = 1,
	macho_rebase_opcode_set_segment_immediate_and_offset_uleb = 2,
	macho_rebase_opcode_add_address_uleb = 3,
	macho_rebase_opcode_add_immediate_scaled = 4,
	macho_rebase_opcode_perform_rebase_immediate_times = 5,
	macho_rebase_opcode_perform_rebase_uleb_times = 6,
	macho_rebase_opcode_perform_rebase_add_uleb = 7,
	macho_rebase_opcode_perform_rebase_uleb_times_skipping_uleb = 8,
};

LIBMACHO_ENUM(uint8_t, macho_bind_opcode) {
	macho_bind_opcode_done = 0,
	macho_bind_opcode_set_dylib_ordinal_immediate = 1,
	macho_bind_opcode_set_dylib_ordinal_uleb = 2,
	macho_bind_opcode_set_dylib_special_immediate = 3,
	macho_bind_opcode_set_symbol_trailing_flags = 4,
	macho_bind_opcode_set_type_immediate = 5,
	macho_bind_opcode_set_addend_sleb = 6,
	macho_bind_opcode_set_segment_immediate_and_offset_uleb = 7,
	macho_bind_opcode_add_address_uleb = 8,
	macho_bind_opcode_perform_bind = 9,
	macho_bind_opcode_perform_bind_add_address_uleb = 10,
	macho_bind_opcode_perform_bind_add_address_immediate_scaled = 11,
	macho_bind_opcode_perform_bind_uleb_times_skipping_uleb = 12,
	macho_bind_opcode_threaded = 13,
};

LIBMACHO_ENUM(uint8_t, macho_bind_subopcode_threaded) {
	macho_bind_subopcode_threaded_set_bind_ordinal_table_size_uleb = 0,
	macho_bind_subopcode_threaded_apply = 1,
};

LIBMACHO_ENUM(uint8_t, macho_export_symbol_kind) {
	macho_export_symbol_kind_regular = 0,
	macho_export_symbol_kind_thread_local = 1,
	macho_export_symbol_kind_absolute = 2,
};

LIBMACHO_ENUM(uint8_t, macho_export_symbol_flags) {
	macho_export_symbol_flag_weak = 1 << 0,
	macho_export_symbol_flag_reexport = 1 << 1,
	macho_export_symbol_flag_stub_and_resolver = 1 << 2,
};

LIBMACHO_ALWAYS_INLINE uint8_t macho_relocation_instruction_get_opcode(uint8_t byte) {
	return byte >> 4;
};

LIBMACHO_ALWAYS_INLINE uint8_t macho_relocation_instruction_get_immediate(uint8_t byte) {
	return byte & 0x0f;
};

LIBMACHO_ALWAYS_INLINE macho_export_symbol_kind_t macho_export_flags_get_kind(uintmax_t raw_flags) {
	return raw_flags & 3ULL;
};

LIBMACHO_ALWAYS_INLINE macho_export_symbol_flags_t macho_export_flags_get(uintmax_t raw_flags) {
	return raw_flags >> 2;
};

LIBMACHO_DECLARATIONS_END;

#endif // _LIBMACHO_LIBMACHO_H_
