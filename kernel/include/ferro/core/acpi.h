/**
 * This file is part of Anillo OS
 * Copyright (C) 2020 Anillo OS Developers
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

#ifndef _FERRO_CORE_ACPI_H_
#define _FERRO_CORE_ACPI_H_

#include <stdint.h>

#include <ferro/base.h>
#include <ferro/error.h>

FERRO_DECLARATIONS_BEGIN;

FERRO_PACKED_STRUCT(facpi_rsdp_legacy) {
	char signature[8];
	uint8_t checksum;
	char oem_id[6];
	uint8_t revision;
	uint32_t rsdt_address;
};

FERRO_PACKED_STRUCT(facpi_rsdp) {
	facpi_rsdp_legacy_t legacy;
	uint32_t length;
	uint64_t xsdt_address;
	uint8_t extended_checksum;
	uint8_t reserved[3];
};

FERRO_PACKED_STRUCT(facpi_sdt_header) {
	char signature[4];
	uint32_t length;
	uint8_t revision;
	uint8_t checksum;
	char oem_id[6];
	char oem_table_id[8];
	uint32_t oem_revision;
	uint32_t creator_id;
	uint32_t creator_revision;
};

FERRO_PACKED_STRUCT(facpi_rsdt) {
	facpi_sdt_header_t header;
	uint32_t table_pointers[];
};

FERRO_PACKED_STRUCT(facpi_xsdt) {
	facpi_sdt_header_t header;
	uint64_t table_pointers[];
};

FERRO_PACKED_STRUCT(facpi_generic_address_structure) {
	uint8_t address_space;
	uint8_t bit_width;
	uint8_t bit_offset;
	uint8_t access_size;
	uint64_t address;
};

FERRO_PACKED_STRUCT(facpi_fadt) {
	facpi_sdt_header_t header;
	uint32_t facs_address;
	uint32_t dsdt_address;

	uint8_t reserved1;

	uint8_t preferred_pm_profile;
	uint16_t sci_interrupt;
	uint32_t smi_command_port;
	uint8_t acpi_enable;
	uint8_t acpi_disable;
	uint8_t s4bios_req;
	uint8_t pstate_control;

	uint32_t pm1a_event_block;
	uint32_t pm1b_event_block;
	uint32_t pm1a_control_block;
	uint32_t pm1b_control_block;
	uint32_t pm2_control_block;
	uint32_t pm_timer_block;
	uint32_t gpe0_block;
	uint32_t gpe1_block;

	uint8_t pm1_event_length;
	uint8_t pm1_control_length;
	uint8_t pm2_control_length;
	uint8_t pm_timer_length;

	uint8_t gpe0_length;
	uint8_t gpe1_length;
	uint8_t gpe1_base;

	uint8_t c_state_control;
	uint16_t worst_c2_latency;
	uint16_t worst_c3_latency;
	uint16_t flush_size;
	uint16_t flush_stride;
	uint8_t duty_offset;
	uint8_t duty_width;
	uint8_t day_alarm;
	uint8_t month_alarm;
	uint8_t century;

	uint16_t boot_architecture_flags;

	uint8_t reserved2;
	uint32_t flags;

	facpi_generic_address_structure_t ResetReg;

	uint8_t reset_value;
	uint8_t reserved3[3];

	uint64_t extended_facs_address;
	uint64_t extended_dsdt_address;

	facpi_generic_address_structure_t extended_pm1a_event_block;
	facpi_generic_address_structure_t extended_pm1b_event_block;
	facpi_generic_address_structure_t extended_pm1a_control_block;
	facpi_generic_address_structure_t extended_pm1b_control_block;
	facpi_generic_address_structure_t extended_pm2_control_block;
	facpi_generic_address_structure_t extended_pm_timer_block;
	facpi_generic_address_structure_t extended_gpe0_block;
	facpi_generic_address_structure_t extended_gpe1_block;
};

FERRO_ENUM(uint8_t, facpi_madt_entry_type) {
	facpi_madt_entry_type_processor_lapic,
	facpi_madt_entry_type_ioapic,
	facpi_madt_entry_type_ioapic_iso,
	facpi_madt_entry_type_ioapic_nmi_source,
	facpi_madt_entry_type_lapic_nmi_interrupts,
	facpi_madt_entry_type_lapic_override,
	facpi_madt_entry_type_processor_lapic_x2 = 9,
};

FERRO_PACKED_STRUCT(facpi_madt_entry_header) {
	facpi_madt_entry_type_t type;
	uint8_t length;
};

// LAPIC = Local APIC (Advanced Programmable Interrupt Controller)
FERRO_PACKED_STRUCT(facpi_madt_entry_processor_lapic) {
	facpi_madt_entry_header_t header;
	uint8_t acpi_processor_id;
	uint8_t apic_id;
	uint32_t flags;
};

FERRO_PACKED_STRUCT(facpi_madt_entry_ioapic) {
	facpi_madt_entry_header_t header;
	uint8_t id;
	uint8_t reserved;
	uint32_t address;
	uint32_t gsi_base;
};

// ISO = interrupt source override
FERRO_PACKED_STRUCT(facpi_madt_entry_ioapic_iso) {
	facpi_madt_entry_header_t header;
	uint8_t bus_source;
	uint8_t irq_source;
	uint32_t gsi;
	uint16_t flags;
};

FERRO_PACKED_STRUCT(facpi_madt_entry_ioapic_nmi_source) {
	facpi_madt_entry_header_t header;
	uint8_t nmi_source;
	uint8_t reserved;
	uint16_t flags;
	uint32_t gsi;
};

// NMI = non-maskable interrupt
FERRO_PACKED_STRUCT(facpi_madt_entry_lapic_nmi_interrupts) {
	facpi_madt_entry_header_t header;
	uint8_t acpi_processor_id;
	uint16_t flags;
	uint8_t lint_number;
};

FERRO_PACKED_STRUCT(facpi_madt_entry_lapic_override) {
	facpi_madt_entry_header_t header;
	uint16_t reserved;
	uint64_t address;
};

FERRO_PACKED_STRUCT(facpi_madt_entry_processor_lapic_x2) {
	facpi_madt_entry_header_t header;
	uint16_t reserved;
	uint32_t apic_x2_id;
	uint32_t flags;
	uint32_t acpi_id;
};

FERRO_PACKED_STRUCT(facpi_madt) {
	facpi_sdt_header_t header;
	uint32_t lapic_address;
	uint32_t flags;
	uint8_t entries[];
};

/**
 * Initializes the ACPI subsystem.
 *
 * @param physical_rsdp Pointer containing the *physical* address of the RSDP pointer. May NOT be `NULL`.
 */
void facpi_init(facpi_rsdp_t* physical_rsdp);

/**
 * Finds the ACPI with the given name.
 *
 * @param name The name of the table to find. This is compared with the table's `signature` field and must match exactly.
 *
 * @returns `NULL` if no such table exists or a pointer to the table if it does.
 */
facpi_sdt_header_t* facpi_find_table(const char* name);

/**
 * Registers the given ACPI table with the ACPI subsystem, allowing it to be retrieved later with `facpi_find_table`.
 *
 * The table MUST remain valid for as long as it is registered.
 *
 * @param table The table to register.
 *
 * Return values:
 * @retval ferr_ok               The table was registered successfully.
 * @retval ferr_temporary_outage The system did not have enough resources to register the table at this time.
 */
FERRO_WUR ferr_t facpi_register_table(facpi_sdt_header_t* table);

FERRO_DECLARATIONS_END;

#endif // _FERRO_CORE_ACPI_H_
