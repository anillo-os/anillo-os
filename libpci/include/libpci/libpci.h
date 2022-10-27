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

#ifndef _LIBPCI_LIBPCI_H_
#define _LIBPCI_LIBPCI_H_

#include <libpci/base.h>
#include <libpci/objects.h>

LIBPCI_DECLARATIONS_BEGIN;

LIBPCI_STRUCT(pci_device_info) {
	uint16_t vendor_id;
	uint16_t device_id;
	uint8_t class_code;
	uint8_t subclass_code;
	uint8_t programming_interface;
};

LIBPCI_OBJECT_CLASS(device);

typedef bool (*pci_visitor_f)(void* context, const pci_device_info_t* device);
typedef void (*pci_device_interrupt_handler_f)(void* context, pci_device_t* device);

LIBPCI_WUR ferr_t pci_visit(pci_visitor_f iterator, void* context);

LIBPCI_WUR ferr_t pci_connect(const pci_device_info_t* target, pci_device_t** out_device);

LIBPCI_WUR ferr_t pci_device_register_interrupt_handler(pci_device_t* device, pci_device_interrupt_handler_f interrupt_handler, void* context);
LIBPCI_WUR ferr_t pci_device_get_mapped_bar(pci_device_t* device, uint8_t bar_index, sys_shared_memory_t** out_bar, size_t* out_bar_size);
LIBPCI_WUR ferr_t pci_device_enable_bus_mastering(pci_device_t* device);
LIBPCI_WUR ferr_t pci_device_config_space_read(pci_device_t* device, size_t offset, uint8_t size, void* out_data);
LIBPCI_WUR ferr_t pci_device_config_space_write(pci_device_t* device, size_t offset, uint8_t size, const void* data);

LIBPCI_DECLARATIONS_END;

#endif // _LIBPCI_LIBPCI_H_
