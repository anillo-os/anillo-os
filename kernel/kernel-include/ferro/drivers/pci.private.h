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

/**
 * @file
 *
 * An interface for managing the PCI subsystem; private components.
 */

#ifndef _FERRO_DRIVERS_PCI_PRIVATE_H_
#define _FERRO_DRIVERS_PCI_PRIVATE_H_

#include <ferro/drivers/pci.h>

#include <ferro/core/acpi.h>
#include <ferro/core/locks.h>

#include <libsimple/libsimple.h>

FERRO_DECLARATIONS_BEGIN;

FERRO_PACKED_STRUCT(fpci_mcfg_entry) {
	uint64_t base_address;
	uint16_t segment_group;
	uint8_t bus_number_start;
	uint8_t bus_number_end;
	char reserved[4];
};

FERRO_PACKED_STRUCT(fpci_mcfg) {
	facpi_sdt_header_t header;
	char reserved[8];
	fpci_mcfg_entry_t entries[];
};

FERRO_STRUCT(fpci_bus_info) {
	uint8_t location;
	simple_ghmap_t devices;
	flock_spin_intsafe_t devices_lock;
	const fpci_mcfg_entry_t* mcfg_entry;
};

FERRO_STRUCT_FWD(fpci_function_info);

FERRO_STRUCT(fpci_device_info) {
	uint8_t location;
	fpci_bus_info_t* bus;
	simple_ghmap_t functions;
	flock_spin_intsafe_t functions_lock;

	/**
	 * The first function of this device.
	 *
	 * Every device must have at least 1 function, and that's function 0.
	 *
	 * Because this function is mandatory and is very useful for retrieving
	 * information about the device, a pointer to its information structure
	 * is stored here in the device information structure in addition to the
	 * functions hashmap. This enables faster access to it.
	 */
	fpci_function_info_t* function0;
};

FERRO_STRUCT(fpci_function_info) {
	uint8_t location;
	fpci_device_info_t* device;
	volatile uint32_t* mmio_base;
	uint16_t vendor_id;
	uint16_t device_id;
	uint8_t class_code;
	uint8_t subclass_code;
	uint8_t programming_interface;
};

ferr_t fpci_bus_lookup(uint8_t bus, bool create_if_absent, fpci_bus_info_t** out_bus);
ferr_t fpci_device_lookup(fpci_bus_info_t* bus, uint8_t device, bool create_if_absent, fpci_device_info_t** out_device);
ferr_t fpci_function_lookup(fpci_device_info_t* device, uint8_t function, bool create_if_absent, fpci_function_info_t** out_function);

ferr_t fpci_bus_scan(fpci_bus_info_t* bus);
ferr_t fpci_device_scan(fpci_device_info_t* device);
ferr_t fpci_function_scan(fpci_function_info_t* function);

FERRO_DECLARATIONS_END;

#endif // _FERRO_DRIVERS_PCI_PRIVATE_H_
