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

#ifndef _LIBPCI_DEVICE_PRIVATE_H_
#define _LIBPCI_DEVICE_PRIVATE_H_

#include <libpci/base.h>
#include <libpci/objects.private.h>
#include <libpci/libpci.h>
#include <libeve/libeve.h>

LIBPCI_DECLARATIONS_BEGIN;

LIBPCI_STRUCT(pci_device_object) {
	pci_object_t object;
	eve_channel_t* channel;
	pci_device_interrupt_handler_f interrupt_handler;
	void* interrupt_handler_context;
};

LIBPCI_DECLARATIONS_END;

#endif // _LIBPCI_DEVICE_PRIVATE_H_
