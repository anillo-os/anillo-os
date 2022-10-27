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
#include <ferro/core/aarch64/gic.h>

static ferr_t farch_pci_setup_msi_interrupt(farch_gic_interrupt_handler_f handler, void* context, uint32_t* out_msi_data, uint64_t* out_msi_address) {
	ferr_t status = ferr_ok;
	uint64_t interrupt = UINT64_MAX;
	uint32_t msi_data = 0;
	uint64_t msi_address = 0;

	status = farch_gic_allocate_msi_interrupt(&interrupt, &msi_data, &msi_address);
	if (status != ferr_ok) {
		goto out;
	}

	status = farch_gic_interrupt_priority_write(interrupt, 1);
	if (status != ferr_ok) {
		goto out;
	}

	status = farch_gic_interrupt_target_core_write(interrupt, farch_gic_current_core_id());
	if (status != ferr_ok) {
		goto out;
	}

	status = farch_gic_interrupt_configuration_write(interrupt, farch_gic_interrupt_configuration_edge_triggered);
	if (status != ferr_ok) {
		goto out;
	}

	status = farch_gic_interrupt_pending_write(interrupt, false);
	if (status != ferr_ok) {
		goto out;
	}

	status = farch_gic_register_handler(interrupt, true, handler, context);
	if (status != ferr_ok) {
		goto out;
	}

	status = farch_gic_interrupt_enabled_write(interrupt, true);
	if (status != ferr_ok) {
		goto out;
	}

	*out_msi_data = msi_data;
	*out_msi_address = msi_address;

out:
	return status;
};

static void farch_pci_msi_handler(void* data, fint_frame_t* frame) {
	fpci_capability_info_t* msi = data;
	if (msi->function->handler.handler) {
		msi->function->handler.handler(msi->function->handler.data);
	}
};

ferr_t farch_pci_function_register_msi_handler(fpci_capability_info_t* msi) {
	ferr_t status = ferr_ok;
	uint16_t message_control;
	bool is_64_bit = false;
	uint32_t msi_data = 0;
	uint64_t msi_address = 0;

	status = farch_pci_setup_msi_interrupt(farch_pci_msi_handler, msi, &msi_data, &msi_address);
	if (status != ferr_ok) {
		goto out;
	}

	message_control = msi->mmio_base[0] >> 16;

	// make sure only 1 interrupt is enabled and MSI is disabled
	message_control = message_control & 0xfff0;

	msi->mmio_base[0] = (message_control << 16) | (msi->mmio_base[0] & 0xffff);

	msi->mmio_base[1] = msi_address & 0xffffffff;

	is_64_bit = (message_control & (1 << 7)) != 0;

	if (is_64_bit) {
		msi->mmio_base[2] = msi_address >> 32;
	}

	msi->mmio_base[is_64_bit ? 3 : 2] = msi_data & 0xffff;

out:
	return status;
};

static void farch_pci_msi_x_handler(void* data, fint_frame_t* frame) {
	fpci_function_info_t* function = data;
	if (function->handler.handler) {
		function->handler.handler(function->handler.data);
	}
};

ferr_t farch_pci_function_register_msi_x_handler(fpci_function_info_t* function, volatile fpci_msi_x_entry_t* table, size_t entry_count) {
	ferr_t status = ferr_ok;
	uint32_t msi_data = 0;
	uint64_t msi_address = 0;

	status = farch_pci_setup_msi_interrupt(farch_pci_msi_x_handler, function, &msi_data, &msi_address);
	if (status != ferr_ok) {
		goto out;
	}

	// map all interrupts to the same handler for now
	// TODO: allow interrupts to be directed to different handlers
	for (size_t i = 0; i < entry_count; ++i) {
		table[i].message_address_low = msi_address & 0xffffffff;
		table[i].message_address_high = msi_address >> 32;

		table[i].message_data = msi_data;

		// unmasked
		table[i].vector_control = 0;
	}

out:
	return status;
};
