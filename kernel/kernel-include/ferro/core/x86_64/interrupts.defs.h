/*
 * This file is part of Anillo OS
 * Copyright (C) 2022 Anillo OS Developers
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

#ifndef _FERRO_CORE_X86_64_INTERRUPTS_DEFS_H_
#define _FERRO_CORE_X86_64_INTERRUPTS_DEFS_H_

#include <ferro/core/x86_64/interrupts.before.h>

FERRO_DECLARATIONS_BEGIN;

FERRO_ENUM(uint8_t, farch_int_gdt_index) {
	farch_int_gdt_index_null,
	farch_int_gdt_index_code,
	farch_int_gdt_index_data,
	farch_int_gdt_index_tss,
	farch_int_gdt_index_tss_other,
	farch_int_gdt_index_data_user,
	farch_int_gdt_index_code_user,
};

FERRO_OPTIONS(uint64_t, farch_int_gdt_flags) {
	farch_int_gdt_flag_accessed     = 1ULL << 40,
	farch_int_gdt_flag_writable     = 1ULL << 41,
	farch_int_gdt_flag_executable   = 1ULL << 43,
	farch_int_gdt_flag_user_segment = 1ULL << 44,
	farch_int_gdt_flag_dpl_ring_3   = 3ULL << 45,
	farch_int_gdt_flag_present      = 1ULL << 47,
	farch_int_gdt_flag_long         = 1ULL << 53,

	farch_int_gdt_flags_common      = farch_int_gdt_flag_accessed | farch_int_gdt_flag_writable | farch_int_gdt_flag_present | farch_int_gdt_flag_user_segment,

	// just to shut clang up
	farch_int_gdt_flag_dpl_ring_3_hi = 1ULL << 46,
	farch_int_gdt_flag_dpl_ring_3_lo = 1ULL << 45,
};

FERRO_PACKED_STRUCT(farch_int_gdt) {
	uint64_t entries[8];
};

FERRO_OPTIONS(uint16_t, farch_int_idt_entry_options) {
	farch_int_idt_entry_option_enable_interrupts = 1 << 8,
	farch_int_idt_entry_option_present           = 1 << 15,
};

FERRO_PACKED_STRUCT(farch_int_idt_entry) {
	uint16_t pointer_low_16;
	uint16_t code_segment_index;
	uint16_t options;
	uint16_t pointer_mid_16;
	uint32_t pointer_high_32;
	uint32_t reserved;
};

/**
 * Here are the function types of each of the following interrupt entries:
 * ```c
 * fint_isr_t division_error;
 * fint_isr_t debug;
 * fint_isr_t nmi;
 * fint_isr_t breakpoint;
 * fint_isr_t overflow;
 * fint_isr_t bounds_check_failure;
 * fint_isr_t invalid_opcode;
 * fint_isr_t device_not_available;
 * fint_isr_with_code_noreturn_t double_fault;
 * fint_isr_t reserved_9;
 * fint_isr_with_code_t invalid_tss;
 * fint_isr_with_code_t segment_not_present;
 * fint_isr_with_code_t stack_segment_fault;
 * fint_isr_with_code_t general_protection_fault;
 * fint_isr_with_code_t page_fault;
 * fint_isr_t reserved_15;
 * fint_isr_t x87_exception;
 * fint_isr_with_code_t alignment_check_failure;
 * fint_isr_noreturn_t machine_check;
 * fint_isr_t simd_exception;
 * fint_isr_t virtualization_exception;
 * fint_isr_t reserved_21;
 * fint_isr_t reserved_22;
 * fint_isr_t reserved_23;
 * fint_isr_t reserved_24;
 * fint_isr_t reserved_25;
 * fint_isr_t reserved_26;
 * fint_isr_t reserved_27;
 * fint_isr_t reserved_28;
 * fint_isr_t reserved_29;
 * fint_isr_with_code_t security_exception;
 * fint_isr_t reserved_31;
 * 
 * fint_isr_t interrupts[224];
 * ```
 */
FERRO_PACKED_STRUCT(farch_int_idt) {
	farch_int_idt_entry_t division_error;
	farch_int_idt_entry_t debug;
	farch_int_idt_entry_t nmi;
	farch_int_idt_entry_t breakpoint;
	farch_int_idt_entry_t overflow;
	farch_int_idt_entry_t bounds_check_failure;
	farch_int_idt_entry_t invalid_opcode;
	farch_int_idt_entry_t device_not_available;
	farch_int_idt_entry_t double_fault;
	farch_int_idt_entry_t reserved_9;
	farch_int_idt_entry_t invalid_tss;
	farch_int_idt_entry_t segment_not_present;
	farch_int_idt_entry_t stack_segment_fault;
	farch_int_idt_entry_t general_protection_fault;
	farch_int_idt_entry_t page_fault;
	farch_int_idt_entry_t reserved_15;
	farch_int_idt_entry_t x87_exception;
	farch_int_idt_entry_t alignment_check_failure;
	farch_int_idt_entry_t machine_check;
	farch_int_idt_entry_t simd_exception;
	farch_int_idt_entry_t virtualization_exception;
	farch_int_idt_entry_t reserved_21;
	farch_int_idt_entry_t reserved_22;
	farch_int_idt_entry_t reserved_23;
	farch_int_idt_entry_t reserved_24;
	farch_int_idt_entry_t reserved_25;
	farch_int_idt_entry_t reserved_26;
	farch_int_idt_entry_t reserved_27;
	farch_int_idt_entry_t reserved_28;
	farch_int_idt_entry_t reserved_29;
	farch_int_idt_entry_t security_exception;
	farch_int_idt_entry_t reserved_31;

	farch_int_idt_entry_t interrupts[224];
};

FERRO_PACKED_STRUCT(farch_int_idt_pointer) {
	uint16_t limit;
	farch_int_idt_t* base;
};

FERRO_PACKED_STRUCT(farch_int_idt_legacy_pointer) {
	uint16_t limit;
	uint32_t base;
};

FERRO_PACKED_STRUCT(farch_int_gdt_pointer) {
	uint16_t limit;
	farch_int_gdt_t* base;
};

FERRO_PACKED_STRUCT(farch_int_gdt_legacy_pointer) {
	uint16_t limit;
	uint32_t base;
};

FERRO_PACKED_STRUCT(farch_int_tss) {
	uint32_t reserved1;
	uint64_t pst[3];
	uint64_t reserved2;
	uint64_t ist[7];
	uint64_t reserved3;
	uint16_t reserved4;
	uint16_t iomap_offset;
};

FERRO_ENUM(uint8_t, farch_int_ist_index) {
	// used for all interrupts without their own IST stack
	farch_int_ist_index_generic_interrupt,

	// used for the double fault handler
	farch_int_ist_index_double_fault,

	// used for the debug handler
	farch_int_ist_index_debug,

	farch_int_ist_index_page_fault,
};

FERRO_DECLARATIONS_END;

#endif // _FERRO_CORE_X86_64_INTERRUPTS_DEFS_H_
