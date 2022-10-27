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

#include <ferro/drivers/pci.private.h>

#include <ferro/core/interrupts.h>
#include <ferro/core/cpu.h>
#include <ferro/core/x86_64/apic.h>

static void farch_pci_msi_handler(void* data, fint_frame_t* frame) {
	fpci_capability_info_t* msi = data;
	if (msi->function->handler.handler) {
		msi->function->handler.handler(msi->function->handler.data);
	}
	farch_apic_signal_eoi();
};

ferr_t farch_pci_function_register_msi_handler(fpci_capability_info_t* msi) {
	ferr_t status = ferr_ok;
	uint16_t message_control;
	uint8_t interrupt;
	bool is_64_bit = false;

	// disable interrupts to prevent this thread from migrating between CPUs
	// TODO: introduce a way to pin a thread to a CPU without disabling interrupts
	fint_disable();

	status = farch_int_register_next_available(farch_pci_msi_handler, msi, &interrupt);
	if (status != ferr_ok) {
		goto out;
	}

	message_control = msi->mmio_base[0] >> 16;

	// make sure only 1 interrupt is enabled and MSI is disabled
	message_control = message_control & 0xfff0;

	msi->mmio_base[0] = (message_control << 16) | (msi->mmio_base[0] & 0xffff);

	msi->mmio_base[1] = (0xfee << 20) | (fcpu_id() << 12);

	is_64_bit = (message_control & (1 << 7)) != 0;

	if (is_64_bit) {
		msi->mmio_base[2] = 0;
	}

	// edge triggered, fixed destination
	msi->mmio_base[is_64_bit ? 3 : 2] = interrupt;

out:
	fint_enable();
	return status;
};

static void farch_pci_msi_x_handler(void* data, fint_frame_t* frame) {
	fpci_function_info_t* function = data;
	if (function->handler.handler) {
		function->handler.handler(function->handler.data);
	}
	farch_apic_signal_eoi();
};

ferr_t farch_pci_function_register_msi_x_handler(fpci_function_info_t* function, volatile fpci_msi_x_entry_t* table, size_t entry_count) {
	ferr_t status = ferr_ok;
	uint8_t interrupt;

	// disable interrupts to prevent this thread from migrating between CPUs
	// TODO: introduce a way to pin a thread to a CPU without disabling interrupts
	fint_disable();

	status = farch_int_register_next_available(farch_pci_msi_x_handler, function, &interrupt);
	if (status != ferr_ok) {
		goto out;
	}

	// map all interrupts to the same handler for now
	// TODO: allow interrupts to be directed to different handlers
	for (size_t i = 0; i < entry_count; ++i) {
		table[i].message_address_low = (0xfee << 20) | (fcpu_id() << 12);
		table[i].message_address_high = 0;

		// edge triggered, fixed destination
		table[i].message_data = interrupt;

		// unmasked
		table[i].vector_control = 0;
	}

out:
	fint_enable();
	return status;
};
