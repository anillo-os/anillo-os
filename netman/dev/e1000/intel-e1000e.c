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

#include <netman/dev/e1000/e1000.private.h>
#include <libpci/libpci.h>
#include <libsys/libsys.h>
#include <libsimple/libsimple.h>
#include <netman/device.private.h>

#ifndef NETMAN_E1000_LOG_INTERRUPTS
	#define NETMAN_E1000_LOG_INTERRUPTS 0
#endif

// TODO: update code to use memory pool for physical memory allocations

static void netman_e1000_interrupt_handler(void* data, pci_device_t* device) {
	netman_e1000_t* nic = data;
	uint32_t cause = E1000_READ_REG(&nic->library_handle, E1000_ICR);
	bool rx = false;
	bool tx = false;

#if NETMAN_E1000_LOG_INTERRUPTS
	sys_console_log_f("Intel E1000e: interrupt received (cause = %016x)\n", cause);
#endif

	if ((cause & (E1000_ICR_RXT0 | E1000_ICR_RXO | E1000_ICR_RXDMT0)) != 0) {
		rx = true;
	}

	if ((cause & (E1000_ICR_TXDW | E1000_ICR_TXQE | E1000_ICR_TXD_LOW)) != 0) {
		tx = true;
	}

	netman_device_schedule_poll(nic->net_device, rx, tx);
};

static void netman_e1000_rx_init(netman_e1000_t* nic) {
	uint32_t tmp;

	nic->rx_ring_count = NETMAN_INTEL_E1000E_RX_RING_COUNT_DEFAULT;

	// allocate a ring of descriptors
	size_t ring_page_count = sys_page_round_up_count(sizeof(netman_e1000_rx_descriptor_t) * nic->rx_ring_count);
	sys_abort_status_log(sys_page_allocate(ring_page_count, sys_page_flag_contiguous | sys_page_flag_prebound | sys_page_flag_uncacheable, (void*)&nic->rx_ring));
	sys_abort_status_log(sys_page_translate((void*)nic->rx_ring, (void*)&nic->rx_ring_phys));

	sys_abort_status_log(sys_mempool_allocate(nic->rx_ring_count * sizeof(*nic->rx_ring_virt_addrs), NULL, (void*)&nic->rx_ring_virt_addrs));

	// now allocate and map buffers
	size_t ring_buffer_page_count = sys_page_round_up_count(NETMAN_INTEL_E1000E_RX_RING_BUFFER_SIZE);
	for (size_t i = 0; i < nic->rx_ring_count; ++i) {
		volatile netman_e1000_rx_descriptor_t* desc = &nic->rx_ring[i];
		uint64_t phys_addr = 0;

		sys_abort_status_log(sys_page_allocate(ring_buffer_page_count, sys_page_flag_contiguous | sys_page_flag_prebound | sys_page_flag_uncacheable, &nic->rx_ring_virt_addrs[i]));
		sys_abort_status_log(sys_page_translate(nic->rx_ring_virt_addrs[i], &phys_addr));

		desc->address = phys_addr;
		desc->status = 0;
	}

	// write the ring base address into the appropriate registers
	E1000_WRITE_REG(&nic->library_handle, E1000_RDBAL(0), (uint64_t)nic->rx_ring_phys & 0xffffffff);
	E1000_WRITE_REG(&nic->library_handle, E1000_RDBAH(0), (uint64_t)nic->rx_ring_phys >> 32);

	// write the size of the ring buffer into the rdlen register
	E1000_WRITE_REG(&nic->library_handle, E1000_RDLEN(0), sizeof(netman_e1000_rx_descriptor_t) * nic->rx_ring_count);

	// set up the head and tail registers
	E1000_WRITE_REG(&nic->library_handle, E1000_RDH(0), 0);
	// IMPORTANT:
	// the tail descriptor points to the buffer just after the area owned by the hardware.
	// the descriptor at the tail pointer is the first one in the area owned by the software.
	//
	// the hardware documentation is confusing about valid values for the tail.
	// it says that it points to the index just after the last one owned by the hardware.
	// it's unclear whether this means it can be set to RDLEN (which would technically fall outside
	// the ring buffer area).
	//
	// to err on the side of caution, we choose to have it always be an index within the ring buffer, NOT outside it.
	// unfortunately, this means that one descriptor is always wasted.
	E1000_WRITE_REG(&nic->library_handle, E1000_RDT(0), nic->rx_ring_count - 1);

	// set up the receive address
	e1000_rar_set(&nic->library_handle, nic->net_device->mac_address, 0);

	// set up the interrupt delay timer
	E1000_WRITE_REG(&nic->library_handle, E1000_RDTR, 0);

	// finally, set up the receive control register
	//
	// we cannot simply overwrite it, as the library may have set some bits in it that we should preserve.
	tmp = E1000_READ_REG(&nic->library_handle, E1000_RCTL);

	// set the buffer size
	tmp |= (E1000_RCTL_SZ_4096 | E1000_RCTL_BSEX);

	tmp &= ~(
		E1000_RCTL_VFE | // disable VLAN filtering
		E1000_RCTL_LPE | // disable long packet reception
		E1000_RCTL_SBP   // do not store bad packets
	);
	tmp |= (
		E1000_RCTL_SECRC      | // strip Ethernet CRC
		E1000_RCTL_BAM        | // broadcast accept mode
		E1000_RCTL_RDMTS_HALF   // interrupt when receive queue is half full
	);

	E1000_WRITE_REG(&nic->library_handle, E1000_RCTL, tmp);
};

static void netman_e1000_tx_init(netman_e1000_t* nic) {
	uint32_t tmp;

	nic->tx_ring_count = NETMAN_INTEL_E1000E_TX_RING_COUNT_DEFAULT;

	// allocate a ring of descriptors
	size_t ring_page_count = sys_page_round_up_count(sizeof(netman_e1000_tx_descriptor_t) * nic->tx_ring_count);
	sys_abort_status_log(sys_page_allocate(ring_page_count, sys_page_flag_contiguous | sys_page_flag_prebound | sys_page_flag_uncacheable, (void*)&nic->tx_ring));
	sys_abort_status_log(sys_page_translate((void*)nic->tx_ring, (void*)&nic->tx_ring_phys));

	sys_abort_status_log(sys_mempool_allocate(nic->tx_ring_count * sizeof(*nic->tx_ring_virt_addrs), NULL, (void*)&nic->tx_ring_virt_addrs));

	// now initialize buffers
	size_t ring_buffer_page_count = sys_page_round_up_count(NETMAN_INTEL_E1000E_RX_RING_BUFFER_SIZE);
	for (size_t i = 0; i < nic->tx_ring_count; ++i) {
		volatile netman_e1000_tx_descriptor_t* desc = &nic->tx_ring[i];

		desc->address = 0;
		desc->status_and_extended_command = 0;
		desc->command = 0;
	}

	// write the ring base address into the appropriate registers
	E1000_WRITE_REG(&nic->library_handle, E1000_TDBAL(0), (uint64_t)nic->tx_ring_phys & 0xffffffff);
	E1000_WRITE_REG(&nic->library_handle, E1000_TDBAH(0), (uint64_t)nic->tx_ring_phys >> 32);

	// write the size of the ring buffer into the tdlen register
	E1000_WRITE_REG(&nic->library_handle, E1000_TDLEN(0), sizeof(netman_e1000_tx_descriptor_t) * nic->tx_ring_count);

	// set up the head and tail registers
	E1000_WRITE_REG(&nic->library_handle, E1000_TDH(0), 0);
	E1000_WRITE_REG(&nic->library_handle, E1000_TDT(0), 0);

	// set up the interrupt delay timer
	E1000_WRITE_REG(&nic->library_handle, E1000_TIDV, 0);

	// finally, set up the transmit control register
	//
	// like the receive control register, we cannot simply overwrite it.

	tmp = E1000_READ_REG(&nic->library_handle, E1000_TCTL);

	tmp |= E1000_TCTL_PSP; // pad short packets

	// on newer hardware, enable multiple simultaneous packet reads
	if (nic->library_handle.mac.type >= e1000_82571) {
		tmp |= E1000_TCTL_MULR;
	}

	E1000_WRITE_REG(&nic->library_handle, E1000_TCTL, tmp);
};

static void netman_e1000_rx_poll(netman_device_t* dev) {
	netman_e1000_t* nic = dev->private_data;
	uint32_t read_head = (E1000_READ_REG(&nic->library_handle, E1000_RDT(0)) + 1) % nic->rx_ring_count;
	uint32_t init_read_head = read_head;

	size_t ring_buffer_page_count = sys_page_round_up_count(NETMAN_INTEL_E1000E_RX_RING_BUFFER_SIZE);
	for (volatile netman_e1000_rx_descriptor_t* desc = &nic->rx_ring[read_head]; (desc->status & netman_e1000_rx_status_ready) != 0; read_head = (read_head + 1) % nic->rx_ring_count, desc = &nic->rx_ring[read_head]) {
		uint64_t phys_addr = 0;
		void** virt_ptr = &nic->rx_ring_virt_addrs[read_head];

		if (desc->address) {
			netman_device_rx_queue(dev, (desc->errors != 0) ? NULL : *virt_ptr, desc->length, (desc->status & netman_e1000_rx_status_end_of_packet) != 0, desc->checksum);
		}

		// since we've transferred ownership of the buffer to the net device, we now need to allocate a new buffer

		// it's okay for the address to be NULL; the hardware simply skips the descriptor after setting its "ready" bit
		NETMAN_WUR_IGNORE(sys_page_allocate(ring_buffer_page_count, sys_page_flag_contiguous | sys_page_flag_prebound | sys_page_flag_uncacheable, virt_ptr));
		if (*virt_ptr) {
			NETMAN_WUR_IGNORE(sys_page_translate(*virt_ptr, &phys_addr));
		}
		desc->address = phys_addr;

		// reset the status for it to be re-used
		desc->status = 0;
	}

	// write out the new tail (if it changed)
	if (read_head != init_read_head) {
		E1000_WRITE_REG(&nic->library_handle, E1000_RDT(0), ((read_head == 0) ? nic->rx_ring_count : read_head) - 1);
	}
};

void netman_e1000_rx_enable(netman_e1000_t* nic) {
	uint32_t tmp = E1000_READ_REG(&nic->library_handle, E1000_RCTL);
	tmp |= E1000_RCTL_EN;
	E1000_WRITE_REG(&nic->library_handle, E1000_RCTL, tmp);
};

void netman_e1000_rx_disable(netman_e1000_t* nic) {
	uint32_t tmp = E1000_READ_REG(&nic->library_handle, E1000_RCTL);
	tmp &= ~E1000_RCTL_EN;
	E1000_WRITE_REG(&nic->library_handle, E1000_RCTL, tmp);
};

static void netman_e1000_tx_poll(netman_device_t* dev) {
	netman_e1000_t* nic = dev->private_data;

	for (volatile netman_e1000_tx_descriptor_t* desc = &nic->tx_ring[nic->tx_oldest_pending_index]; (desc->status_and_extended_command & netman_e1000_tx_status_ready) != 0; nic->tx_oldest_pending_index = (nic->tx_oldest_pending_index + 1) % nic->tx_ring_count, desc = &nic->tx_ring[nic->tx_oldest_pending_index]) {
		if (desc->address) {
			NETMAN_WUR_IGNORE(sys_page_free(nic->tx_ring_virt_addrs[nic->tx_oldest_pending_index]));
			netman_device_tx_complete(dev, nic->tx_oldest_pending_index);
		}

		desc->address = 0;
		desc->status_and_extended_command = 0;
		desc->command = 0;
		nic->tx_ring_virt_addrs[nic->tx_oldest_pending_index] = NULL;
	}
};

void netman_e1000_tx_enable(netman_e1000_t* nic) {
	uint32_t tmp = E1000_READ_REG(&nic->library_handle, E1000_TCTL);
	tmp |= E1000_TCTL_EN;
	E1000_WRITE_REG(&nic->library_handle, E1000_TCTL, tmp);
};

void netman_e1000_tx_disable(netman_e1000_t* nic) {
	uint32_t tmp = E1000_READ_REG(&nic->library_handle, E1000_TCTL);
	tmp &= ~E1000_TCTL_EN;
	E1000_WRITE_REG(&nic->library_handle, E1000_TCTL, tmp);
};

static ferr_t netman_e1000_tx_queue(netman_device_t* dev, void* data, size_t data_length, bool end_of_packet, size_t* out_queue_index) {
	netman_e1000_t* nic = dev->private_data;
	uint32_t tail = E1000_READ_REG(&nic->library_handle, E1000_TDT(0));
	uint32_t next_tail = (tail + 1) % nic->tx_ring_count;
	volatile netman_e1000_tx_descriptor_t* desc = &nic->tx_ring[tail];
	uint64_t phys_addr = 0;

	// we can't use the very last descriptor for the same reason we can't use it for receiving.
	// for us, if the next index after the current tail is the oldest pending descriptor (meaning it still hasn't been sent),
	// which is likely to be where the hardware's head pointer is anyways, we can't add another. this means we waste one
	// descriptor (like for receiving), but it's the only way to do it, since the hardware will stop if head == tail.
	if (next_tail == nic->tx_oldest_pending_index) {
		return ferr_temporary_outage;
	}

	nic->tx_ring_virt_addrs[tail] = data;
	NETMAN_WUR_IGNORE(sys_page_translate(data, &phys_addr));
	desc->address = (uintptr_t)phys_addr;
	desc->length = data_length;
	desc->command = netman_e1000_tx_command_report_status | netman_e1000_tx_command_insert_fcs | (end_of_packet ? netman_e1000_tx_command_end_of_packet : 0);

	*out_queue_index = tail;

	E1000_WRITE_REG(&nic->library_handle, E1000_TDT(0), next_tail);

	return ferr_ok;
};

static void netman_e1000_poll_return(netman_device_t* dev) {
	netman_e1000_t* nic = dev->private_data;

	// re-enable interrupts
	E1000_WRITE_REG(&nic->library_handle, E1000_IMS, netman_e1000_interrupt_cause_all_known);
};

// STATIC ONLY FOR DEBUGGING PURPOSES
// DO NOT DEPEND ON THIS BEING A STATIC VARIABLE
static netman_e1000_t* nic = NULL;

static netman_device_methods_t netdev_methods = {
	.rx_poll = netman_e1000_rx_poll,
	.tx_poll = netman_e1000_tx_poll,
	.tx_queue = netman_e1000_tx_queue,
	.poll_return = netman_e1000_poll_return,
};

static const netman_e1000_model_info_t card_ids[] = {
	// QEMU card
	{
		.vendor_id = 0x8086,
		.product_id = E1000_DEV_ID_82574L,
	},

	// I219-V11
	{
		.vendor_id = 0x8086,
		.product_id = E1000_DEV_ID_PCH_CMP_I219_V11,
	},

	// TODO: add more card IDs
};

static bool netman_e1000_scan_iterator(void* context, const pci_device_info_t* device_info) {
	pci_device_info_t* out_dev_info = context;

	for (size_t i = 0; i < sizeof(card_ids) / sizeof(*card_ids); ++i) {
		const netman_e1000_model_info_t* model_info = &card_ids[i];

		if (device_info->vendor_id != model_info->vendor_id || device_info->device_id != model_info->product_id) {
			continue;
		}

		simple_memcpy(out_dev_info, device_info, sizeof(*out_dev_info));
		return false;
	}

	return true;
};

// this function was adapted from em_reset() in the FreeBSD driver
static void netman_e1000_reset(netman_e1000_t* nic) {
	uint32_t pba;
	uint16_t rx_buffer_size;

	switch (nic->library_handle.mac.type) {
		/* Total Packet Buffer on these is 48K */
		case e1000_82571:
		case e1000_82572:
		case e1000_80003es2lan:
				pba = E1000_PBA_32K; /* 32K for Rx, 16K for Tx */
			break;

		case e1000_82573: /* 82573: Total Packet Buffer is 32K */
				pba = E1000_PBA_12K; /* 12K for Rx, 20K for Tx */
			break;

		case e1000_82574:
		case e1000_82583:
				pba = E1000_PBA_20K; /* 20K for Rx, 20K for Tx */
			break;

		case e1000_ich8lan:
			pba = E1000_PBA_8K;
			break;

		case e1000_ich9lan:
		case e1000_ich10lan:
			/* Boost Receive side for jumbo frames */
			if (nic->library_handle.mac.max_frame_size > 4096) {
				pba = E1000_PBA_14K;
			} else {
				pba = E1000_PBA_10K;
			}
			break;

		case e1000_pchlan:
		case e1000_pch2lan:
		case e1000_pch_lpt:
		case e1000_pch_spt:
		case e1000_pch_cnp:
			pba = E1000_PBA_26K;
			break;

		default:
			if (nic->library_handle.mac.max_frame_size > 8192) {
				pba = E1000_PBA_40K; /* 40K for Rx, 24K for Tx */
			} else {
				pba = E1000_PBA_48K; /* 48K for Rx, 16K for Tx */
			}
	}
	E1000_WRITE_REG(&nic->library_handle, E1000_PBA, pba);

	rx_buffer_size = ((E1000_READ_REG(&nic->library_handle, E1000_PBA) & 0xffff) << 10);
	nic->library_handle.fc.high_water = rx_buffer_size - (nic->library_handle.mac.max_frame_size + 1023) & ~(1023);
	nic->library_handle.fc.low_water = nic->library_handle.fc.high_water - 1500;

	nic->library_handle.fc.requested_mode = e1000_fc_full;

	if (nic->library_handle.mac.type == e1000_80003es2lan) {
		nic->library_handle.fc.pause_time = 0xffff;
	} else {
		nic->library_handle.fc.pause_time = 0x0680;
	}

	nic->library_handle.fc.send_xon = true;

	switch (nic->library_handle.mac.type) {
		case e1000_pchlan:
			/* Workaround: no TX flow ctrl for PCH */
			nic->library_handle.fc.requested_mode = e1000_fc_rx_pause;
			nic->library_handle.fc.pause_time = 0xFFFF; /* override */
			nic->library_handle.fc.high_water = 0x5000;
			nic->library_handle.fc.low_water = 0x3000;
			nic->library_handle.fc.refresh_time = 0x1000;
			break;

		case e1000_pch2lan:
		case e1000_pch_lpt:
		case e1000_pch_spt:
		case e1000_pch_cnp:
			nic->library_handle.fc.high_water = 0x5C20;
			nic->library_handle.fc.low_water = 0x5048;
			nic->library_handle.fc.pause_time = 0x0650;
			nic->library_handle.fc.refresh_time = 0x0400;
			E1000_WRITE_REG(&nic->library_handle, E1000_PBA, 26);
			break;

		default:
			break;
	}

	sys_console_log("Intel E1000e: issuing reset\n");

	/* Issue a global reset */
	e1000_reset_hw(&nic->library_handle);
	E1000_WRITE_REG(&nic->library_handle, E1000_WUC, 0);

	sys_console_log("Intel E1000e: initializing hardware\n");

	/* and a re-init */
	if (e1000_init_hw(&nic->library_handle) != E1000_SUCCESS) {
		sys_console_log("Failed to initialize hardware");
		sys_abort();
	}

	sys_console_log("Intel E1000e: retrieving PHY info\n");
	e1000_get_phy_info(&nic->library_handle);

	sys_console_log("Intel E1000e: checking for link\n");
	e1000_check_for_link(&nic->library_handle);
};

#define MAX_PCI_CONNECT_TRIES 3

void netman_e1000_init(void) {
	pci_device_t* dev = NULL;
	const netman_e1000_model_info_t* model_info = NULL;
	uint32_t tmp;
	pci_device_info_t dev_info;
	sys_shared_memory_t* bar_mapping = NULL;

	if (pci_visit(netman_e1000_scan_iterator, &dev_info) != ferr_cancelled) {
		sys_console_log("Intel E1000e: network card not found\n");
		return;
	}

	for (size_t i = 0; i < sizeof(card_ids) / sizeof(*card_ids); ++i) {
		const netman_e1000_model_info_t* this_model_info = &card_ids[i];

		if (dev_info.vendor_id != this_model_info->vendor_id || dev_info.device_id != this_model_info->product_id) {
			continue;
		}

		model_info = this_model_info;
		break;
	}

	for (size_t i = 0; i < MAX_PCI_CONNECT_TRIES; ++i) {
		if (pci_connect(&dev_info, &dev) == ferr_ok) {
			break;
		}
	}

	if (!dev) {
		sys_console_log_f("Intel E1000e: failed to connect to network card\n");
		return;
	}

	sys_console_log("Intel E1000e: found card\n");

	sys_abort_status_log(sys_mempool_allocate(sizeof(netman_e1000_t), NULL, (void*)&nic));
	simple_memset(nic, 0, sizeof(*nic));

	nic->device = dev;
	nic->model_info = model_info;
	nic->library_handle.back = nic;

	sys_abort_status_log(pci_device_enable_bus_mastering(nic->device));
	sys_console_log("Intel E1000e: enabled bus mastering\n");

	// initialize info for the library
	sys_abort_status_log(pci_device_config_space_read(nic->device, 0x04, 2, &nic->library_handle.bus.pci_cmd_word));
	nic->library_handle.vendor_id = dev_info.vendor_id;
	nic->library_handle.device_id = dev_info.device_id;
	sys_abort_status_log(pci_device_config_space_read(nic->device, 0x08, 1, &nic->library_handle.revision_id));
	sys_abort_status_log(pci_device_config_space_read(nic->device, 0x2c, 2, &nic->library_handle.subsystem_vendor_id));
	sys_abort_status_log(pci_device_config_space_read(nic->device, 0x2e, 2, &nic->library_handle.subsystem_device_id));

	sys_abort_status_log(pci_device_get_mapped_bar(nic->device, 0, &bar_mapping, &nic->bar0_size));
	sys_abort_status_log(sys_shared_memory_map(bar_mapping, sys_page_round_up_count(nic->bar0_size), 0, (void*)&nic->bar0));
	sys_release(bar_mapping);
	sys_console_log_f("Intel E1000e: mapped BAR0 at %p, %zu bytes\n", nic->bar0, nic->bar0_size);

	// the library also needs to know this address
	// (it doesn't actually access it, though)
	nic->library_handle.hw_addr = (void*)nic->bar0;

	// identify the MAC
	// we need this info for some setup later on
	if (e1000_set_mac_type(&nic->library_handle) != E1000_SUCCESS) {
		sys_console_log_f("Failed to identify MAC");
		sys_abort();
	}

	switch (nic->library_handle.mac.type) {
		// some MACs have a separate flash BAR;
		// let's map it now
		case e1000_ich8lan:
		case e1000_ich9lan:
		case e1000_ich10lan:
		case e1000_pchlan:
		case e1000_pch2lan:
		case e1000_pch_lpt: {
			sys_shared_memory_t* mapping = NULL;
			sys_abort_status_log(pci_device_get_mapped_bar(nic->device, 1, &mapping, &nic->flash_bar_size));
			sys_abort_status_log(sys_shared_memory_map(mapping, sys_page_round_up_count(nic->flash_bar_size), 0, (void*)&nic->flash_bar));
			sys_release(mapping);
			sys_console_log_f("Intel E1000e: mapped flash BAR at %p, %zu bytes\n", nic->flash_bar, nic->flash_bar_size);

			// the library also needs to know this address
			// (it doesn't actually access it, though)
			nic->library_handle.flash_address = (void*)nic->flash_bar;
		} break;

		// newer models have the flash in the BAR0 region, so let's save that address
		case e1000_pch_spt:
		case e1000_pch_cnp: {
			nic->flash_bar = (void*)nic->bar0 + E1000_FLASH_BASE_ADDR;
		} break;

		default:
			break;
	}

	if (e1000_setup_init_funcs(&nic->library_handle, true) != E1000_SUCCESS) {
		sys_console_log("Failed to initialize library functions");
		sys_abort();
	}

	// enable auto-negotiation
	nic->library_handle.mac.autoneg = true;
	nic->library_handle.phy.autoneg_wait_to_complete = false;
	// advertise all valid autoneg values
	nic->library_handle.phy.autoneg_advertised = ADVERTISE_10_HALF | ADVERTISE_10_FULL | ADVERTISE_100_HALF | ADVERTISE_100_FULL | ADVERTISE_1000_FULL;

	// set options for copper media
	if (nic->library_handle.phy.media_type == e1000_media_type_copper) {
		nic->library_handle.phy.mdix = 0;
		nic->library_handle.phy.disable_polarity_correction = false;
		nic->library_handle.phy.ms_type = e1000_ms_hw_default;
	}

	// set a sane default for max frame size
	// (this is the size of an ethernet header + MTU + FCS size)
	nic->library_handle.mac.max_frame_size = 14 + 1500 + ETHERNET_FCS_SIZE;

	nic->library_handle.mac.report_tx_early = true;

	// wait until we're allowed to reset the PHY
	while (e1000_check_reset_block(&nic->library_handle) != E1000_SUCCESS);

	sys_console_log("Intel E1000e: going to perform reset\n");

	// now reset the hardware
	if (e1000_reset_hw(&nic->library_handle) != E1000_SUCCESS) {
		sys_console_log("Failed to reset hardware");
		sys_abort();
	}

	sys_console_log("Intel E1000e: going to read MAC address\n");

	// now read the MAC address
	if (e1000_read_mac_addr(&nic->library_handle) != E1000_SUCCESS) {
		sys_console_log("Failed to read MAC address");
		sys_abort();
	}

	sys_console_log_f("Intel E1000e: MAC address = %02x:%02x:%02x:%02x:%02x:%02x\n", nic->library_handle.mac.addr[0], nic->library_handle.mac.addr[1], nic->library_handle.mac.addr[2], nic->library_handle.mac.addr[3], nic->library_handle.mac.addr[4], nic->library_handle.mac.addr[5]);

	// register a network device
	sys_abort_status_log(netman_device_register(nic->library_handle.mac.addr, &netdev_methods, NETMAN_INTEL_E1000E_TX_RING_COUNT_DEFAULT, &nic->net_device));
	nic->net_device->private_data = nic;

	//
	// initialize interrupts
	//

	// disable all interrupts
	E1000_WRITE_REG(&nic->library_handle, E1000_IMC, 0xffffffff);

	sys_abort_status_log(pci_device_register_interrupt_handler(nic->device, netman_e1000_interrupt_handler, nic));
	sys_console_log("Intel E1000e: registered interrupt handler\n");

	// check if it needs management passthrough
	nic->needs_management_passthrough = e1000_enable_mng_pass_thru(&nic->library_handle);

	// check if it has Active Management Technology (AMT)
	switch (nic->library_handle.mac.type) {
		case e1000_82573:
		case e1000_82583:
		case e1000_ich8lan:
		case e1000_ich9lan:
		case e1000_ich10lan:
		case e1000_pchlan:
		case e1000_pch2lan:
		case e1000_pch_lpt:
		case e1000_pch_spt:
		case e1000_pch_cnp:
			nic->has_amt = true;
			break;

		default:
			break;
	}

	sys_console_log("Intel E1000e: performing reset\n");

	// reset the hardware and get it ready for operation
	netman_e1000_reset(nic);

	sys_console_log("Intel E1000e: reset complete\n");

	// TODO: maybe check link status?

	// for management passthrough without AMT, we need to take control of the hardware
	if (nic->needs_management_passthrough && !nic->has_amt) {
		if (nic->library_handle.mac.type == e1000_82573) {
			E1000_WRITE_REG(&nic->library_handle, E1000_SWSM, E1000_READ_REG(&nic->library_handle, E1000_SWSM) | E1000_SWSM_DRV_LOAD);
		} else {
			E1000_WRITE_REG(&nic->library_handle, E1000_CTRL_EXT, E1000_READ_REG(&nic->library_handle, E1000_CTRL_EXT) | E1000_CTRL_EXT_DRV_LOAD);
		}
	}

	// configure MSI-X (in case we're using that)
	// everything is mapped to vector 0, and everything is enabled
	E1000_WRITE_REG(&nic->library_handle, E1000_IVAR, (1 << 3) | (1 << 7) | (1 << 11) | (1 << 15) | (1 << 19));

	// auto-mask interrupts on read
	tmp = E1000_READ_REG(&nic->library_handle, E1000_CTRL_EXT);
	tmp |= E1000_CTRL_EXT_IAME;
	E1000_WRITE_REG(&nic->library_handle, E1000_CTRL_EXT, tmp);
	E1000_WRITE_REG(&nic->library_handle, E1000_IAM, netman_e1000_interrupt_cause_all_known);

	// set up an interrupt delay
	//
	// this value is in increments of 256ns.
	// the recommended range for this value is 651 to 5580,
	// which corresponds to a range of approximately 166us and 1428us.
	// we set it to 3000, which corresponds to a delay of 768us.
	// this is a fairly arbitrary choice, with the exception that we prefer to keep it larger
	// than the scheduler slice period (which is currently 500us).
	E1000_WRITE_REG(&nic->library_handle, E1000_ITR, 3000);

	sys_console_log("Intel E1000e: going to initialize RX and TX\n");

	// initialize RX and TX
	netman_e1000_rx_init(nic);
	netman_e1000_tx_init(nic);

	// enable all known interrupts and clear pending interrupts
	E1000_WRITE_REG(&nic->library_handle, E1000_IMS, netman_e1000_interrupt_cause_all_known);
	E1000_WRITE_REG(&nic->library_handle, E1000_ICR, netman_e1000_interrupt_cause_all_known);

	// enable interrupts
	E1000_WRITE_REG(&nic->library_handle, E1000_IMC, 0);

	sys_console_log("Intel E1000e: driver set up complete\n");

	sys_console_log("Intel E1000e: enabling receive and transmit\n");

	netman_e1000_rx_enable(nic);
	netman_e1000_tx_enable(nic);
};

uint32_t netman_e1000_read_bar0(netman_e1000_t* nic, size_t offset) {
	return *(volatile uint32_t*)((volatile char*)nic->bar0 + offset);
};

void netman_e1000_write_bar0(netman_e1000_t* nic, size_t offset, uint32_t value) {
	*(volatile uint32_t*)((volatile char*)nic->bar0 + offset) = value;
};

uint32_t netman_e1000_flash_read_32(netman_e1000_t* nic, size_t offset) {
	fassert((offset & 3) == 0);
	return *(volatile uint32_t*)((volatile char*)nic->flash_bar + offset);
};

void netman_e1000_flash_write_32(netman_e1000_t* nic, size_t offset, uint32_t value) {
	fassert((offset & 3) == 0);
	*(volatile uint32_t*)((volatile char*)nic->flash_bar + offset) = value;
};

uint16_t netman_e1000_flash_read_16(netman_e1000_t* nic, size_t offset) {
	fassert((offset & 1) == 0);
	uint32_t shift = ((offset & 3) * 8);
	volatile uint32_t* addr = (volatile uint32_t*)((volatile char*)nic->flash_bar + (offset & ~3));
	uint32_t val = *addr;
	return (val >> shift) & 0xffff;
};

void netman_e1000_flash_write_16(netman_e1000_t* nic, size_t offset, uint16_t value) {
	fassert((offset & 1) == 0);
	uint32_t shift = ((offset & 3) * 8); \
	volatile uint32_t* addr = (volatile uint32_t*)((volatile char*)nic->flash_bar + (offset & ~3)); \
	uint32_t val = *addr; \
	val &= ~((uint32_t)0xffff << shift); \
	val |= (uint32_t)(value) << shift; \
	*addr = val; \
};
