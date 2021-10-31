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
 * A simple ELF library for Anillo OS.
 */

#ifndef _LIBELF_LIBELF_H_
#define _LIBELF_LIBELF_H_

#include <stdint.h>

#include <libelf/base.h>
#include <ferro/platform.h>

// Anillo only supports ELF64, so we only include structure definitions for ELF64 structures

LIBELF_DECLARATIONS_BEGIN;

/**
 * @addtogroup ELF
 *
 * Definitions for ELF files.
 *
 * @{
 */

LIBELF_PACKED_STRUCT(elf_header) {
	// 0x7f followed by "ELF", always in that order
	uint32_t magic;
	uint8_t bits;
	uint8_t endianness;
	uint8_t identifier_version;
	uint8_t abi;
	uint8_t abi_version;
	uint8_t padding[7];
	uint16_t type;
	uint16_t machine;
	uint32_t format_version;
	uint64_t entry;
	uint64_t program_header_table_offset;
	uint64_t section_header_table_offset;
	uint32_t flags;
	uint16_t header_size;
	uint16_t program_header_entry_size;
	uint16_t program_header_entry_count;
	uint16_t section_header_entry_size;
	uint16_t section_header_entry_count;
	uint16_t section_names_entry_index;
};

LIBELF_PACKED_STRUCT(elf_program_header) {
	uint32_t type;
	uint32_t flags;
	uint64_t offset;
	uint64_t virtual_address;
	uint64_t physical_address;
	uint64_t file_size;
	uint64_t memory_size;
	uint64_t alignment;
};

LIBELF_PACKED_STRUCT(elf_section_header) {
	uint32_t name_offset;
	uint32_t type;
	uint64_t flags;
	uint64_t virtual_address;
	uint64_t offset;
	uint64_t file_size;
	uint32_t associated_section_index;
	uint32_t info;
	uint64_t alignment;
	uint64_t entry_size;
};

// the magic value as an integer is endian-dependent
#if ELF_ENDIANNESS == ELF_ENDIANNESS_BIG
	#define ELF_MAGIC 0x7f454c46
#else
	#define ELF_MAGIC 0x464c457f
#endif

#define ELF_IDENTIFIER_VERSION 1
#define ELF_FORMAT_VERSION 1

LIBELF_ENUM(uint8_t, elf_bits) {
	elf_bits_none = 0,
	elf_bits_32   = 1,
	elf_bits_64   = 2,
};

LIBELF_ENUM(uint8_t, elf_endianness) {
	elf_endianness_none   = 0,
	elf_endianness_little = 1,
	elf_endianness_big    = 2,
};

LIBELF_ENUM(uint8_t, elf_abi) {
	elf_abi_sysv     = 0x00,
	elf_abi_hp_ux    = 0x01,
	elf_abi_netbsd   = 0x02,
	elf_abi_linux    = 0x03,
	elf_abi_hurd     = 0x04,
	elf_abi_solaris  = 0x06,
	elf_abi_aix      = 0x07,
	elf_abi_irix     = 0x08,
	elf_abi_freebsd  = 0x09,
	elf_abi_tru64    = 0x0a,
	elf_abi_modesto  = 0x0b,
	elf_abi_openbsd  = 0x0c,
	elf_abi_openvms  = 0x0d,
	elf_abi_nonstop  = 0x0e,
	elf_abi_aros     = 0x0f,
	elf_abi_fenix    = 0x10,
	elf_abi_cloudabi = 0x11,
	elf_abi_openvos  = 0x12,
};

LIBELF_ENUM(uint16_t, elf_type) {
	elf_type_none                           = 0x0000,
	elf_type_relocatable                    = 0x0001,
	elf_type_executable                     = 0x0002,
	elf_type_shared_object                  = 0x0003,
	elf_type_core                           = 0x0004,
	elf_type_os_specific_lower_bound        = 0xfe00,
	elf_type_os_specific_upper_bound        = 0xfeff,
	elf_type_processor_specific_lower_bound = 0xff00,
	elf_type_processor_specific_upper_bound = 0xffff,
};

LIBELF_ENUM(uint16_t, elf_machine) {
	elf_machine_none = 0x00,
	elf_machine_att_we_32100 = 0x01,
	elf_machine_sparc        = 0x02,
	elf_machine_x86          = 0x03,
	elf_machine_68k          = 0x04,
	elf_machine_88k          = 0x05,
	elf_machine_mcu          = 0x06,
	elf_machine_intel_80860  = 0x07,
	elf_machine_mips         = 0x08,
	elf_machine_system_370   = 0x09,
	elf_machine_mips_rs3000  = 0x0a,
	elf_machine_pa_risc      = 0x0e,
	elf_machine_intel_80960  = 0x13,
	elf_machine_ppc32        = 0x14,
	elf_machine_ppc64        = 0x15,
	elf_machine_s390         = 0x16,
	elf_machine_arm32        = 0x28,
	elf_machine_superh       = 0x2a,
	elf_machine_itanium64    = 0x32,
	elf_machine_amd64        = 0x3e,
	elf_machine_tms320c6000  = 0x8c,
	elf_machine_arm64        = 0xb7,
	elf_machine_riscv        = 0xf3,
	elf_machine_wdc_65c816   = 0x101,
};

LIBELF_ENUM(uint32_t, elf_program_header_type) {
	elf_program_header_type_none                           = 0x00000000,
	elf_program_header_type_loadable                       = 0x00000001,
	elf_program_header_type_dynamic_linking_information    = 0x00000002,
	elf_program_header_type_interpreter_information        = 0x00000003,
	elf_program_header_type_miscellaneous_information      = 0x00000004,
	elf_program_header_type_program_header_table           = 0x00000006,
	elf_program_header_type_tls_template                   = 0x00000007,
	elf_program_header_type_os_specific_lower_bound        = 0x60000000,
	elf_program_header_type_os_specific_upper_bound        = 0x6fffffff,
	elf_program_header_type_processor_specific_lower_bound = 0x70000000,
	elf_program_header_type_processor_specific_upper_bound = 0x7fffffff,
};

LIBELF_ENUM(uint32_t, elf_section_header_type) {
	elf_section_header_type_none                                = 0x00000000,
	elf_section_header_type_program_data                        = 0x00000001,
	elf_section_header_type_symbol_table                        = 0x00000002,
	elf_section_header_type_string_table                        = 0x00000003,
	elf_section_header_type_relocation_information_with_addends = 0x00000004,
	elf_section_header_type_symbol_hash_table                   = 0x00000005,
	elf_section_header_type_dynamic_linking_information         = 0x00000006,
	elf_section_header_type_miscellaneous_information           = 0x00000007,
	elf_section_header_type_no_data                             = 0x00000008,
	elf_section_header_type_relocation_information              = 0x00000009,
	elf_section_header_type_dynamic_linker_symbol_table         = 0x0000000b,
	elf_section_header_type_constructors                        = 0x0000000e,
	elf_section_header_type_destructors                         = 0x0000000f,
	elf_section_header_type_preconstructors                     = 0x00000010,
	elf_section_header_type_group                               = 0x00000011,
	elf_section_header_type_section_indices                     = 0x00000012,
	elf_section_header_type_os_specific_lower_bound             = 0x60000000,
};

LIBELF_ENUM(uint64_t, elf_section_flag) {
	elf_section_flag_none                = 0x00000000,
	elf_section_flag_writable            = 0x00000001,
	elf_section_flag_allocate            = 0x00000002,
	elf_section_flag_exectuable          = 0x00000004,
	elf_section_flag_mergeable           = 0x00000010,
	elf_section_flag_strings             = 0x00000020,
	elf_section_flag_info_contains_index = 0x00000040,
	elf_section_flag_preserve_order      = 0x00000080,
	elf_section_flag_os_nonconforming    = 0x00000100,
	elf_section_flag_group_member        = 0x00000200,
	elf_section_flag_tls                 = 0x00000400,
	elf_section_flag_os_specific         = 0x0ff00000,
	elf_section_flag_processor_specific  = 0xf0000000,
};

LIBELF_ENUM(uint32_t, elf_program_header_flags) {
	elf_program_header_flag_execute = 1 << 0,
	elf_program_header_flag_write   = 1 << 1,
	elf_program_header_flag_read    = 1 << 2,
};

/**
 * @}
 */

LIBELF_DECLARATIONS_END;

#endif // _LIBELF_LIBELF_H_
