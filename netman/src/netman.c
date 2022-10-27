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

#include <libeve/libeve.h>
#include <libpci/libpci.h>

#include <netman/dev/e1000/e1000.h>

#include <netman/ether.h>
#include <netman/arp.h>
#include <netman/ip.h>
#include <netman/udp.h>
#include <netman/tcp.h>

static bool pci_device_iterator(void* context, const pci_device_info_t* info) {
	sys_console_log_f("netman: Found PCI device: VID = 0x%04x, DID = 0x%04x, class code = 0x%02x, subclass code = 0x%02x, programming interface = 0x%02x\n", info->vendor_id, info->device_id, info->class_code, info->subclass_code, info->programming_interface);
	return true;
};

static void netman_iterate_devices(void* context) {
	ferr_t status = pci_visit(pci_device_iterator, NULL);

	if (status != ferr_ok && status != ferr_cancelled) {
		sys_abort_status_log(status);
	}
};

extern void netman_testing(void);

static void netman_testing_thread(void* data, sys_thread_t* this_thread) {
	netman_testing();
};

static void netman_init(void* context) {
	netman_e1000_init();

	netman_ether_init();
	netman_arp_init();
	netman_ipv4_init();
	netman_udp_init();
	netman_tcp_init();

	sys_abort_status_log(sys_thread_create(NULL, 512ull * 1024, netman_testing_thread, NULL, sys_thread_flag_resume, NULL));
};

void main(void) {
	sys_abort_status_log(eve_loop_enqueue(eve_loop_get_main(), netman_iterate_devices, NULL));
	sys_abort_status_log(eve_loop_enqueue(eve_loop_get_main(), netman_init, NULL));
	eve_loop_run(eve_loop_get_main());
};
