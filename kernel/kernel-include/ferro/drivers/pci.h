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
 * An interface for managing the PCI subsystem.
 */

#ifndef _FERRO_DRIVERS_PCI_H_
#define _FERRO_DRIVERS_PCI_H_

#include <ferro/base.h>
#include <ferro/error.h>

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

FERRO_DECLARATIONS_BEGIN;

FERRO_STRUCT_FWD(fpage_mapping);

FERRO_STRUCT(fpci_device) {
	uint16_t vendor_id;
	uint16_t device_id;
	uint8_t class_code;
	uint8_t subclass_code;
	uint8_t programming_interface;
};

typedef void (*fpci_device_interrupt_handler_f)(void* data);
typedef bool (*fpci_scan_iterator_f)(void* context, fpci_device_t* device);

void fpci_init(void);

FERRO_WUR ferr_t fpci_lookup(uint16_t vendor_id, uint16_t device_id, fpci_device_t** out_device);

/**
 * @note This function takes some internal locks to prevent device tree modifications while it is iterating through it.
 *       As such, the provided iterator function should NOT make any calls that access the device tree in any way,
 *       even for read-only operations. For example, it should NOT call fpci_lookup().
 */
FERRO_WUR ferr_t fpci_scan(fpci_scan_iterator_f iterator, void* context, fpci_device_t** out_device);

FERRO_WUR ferr_t fpci_device_register_interrupt_handler(fpci_device_t* device, fpci_device_interrupt_handler_f handler, void* data);
FERRO_WUR ferr_t fpci_device_get_mapped_bar(fpci_device_t* device, uint8_t bar_index, volatile uint32_t** out_bar, size_t* out_size);
FERRO_WUR ferr_t fpci_device_get_mapped_bar_mapping(fpci_device_t* device, uint8_t bar_index, fpage_mapping_t** out_mapping, size_t* out_size);
FERRO_WUR ferr_t fpci_device_get_mapped_bar_raw_index(fpci_device_t* device, uint8_t raw_bar_index, volatile uint32_t** out_bar, size_t* out_size);
FERRO_WUR ferr_t fpci_device_enable_bus_mastering(fpci_device_t* device);

FERRO_WUR ferr_t fpci_device_config_space_read(fpci_device_t* device, size_t offset, uint8_t size, void* out_data);
FERRO_WUR ferr_t fpci_device_config_space_write(fpci_device_t* device, size_t offset, uint8_t size, const void* data);

FERRO_DECLARATIONS_END;

#endif // _FERRO_DRIVERS_PCI_H_
