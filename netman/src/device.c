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

#include <netman/device.private.h>
#include <netman/packet.private.h>
#include <libsimple/libsimple.h>
#include <libeve/libeve.h>

// TODO: support more than one device

static netman_device_t* global_device = NULL;
static sys_mutex_t global_device_lock = SYS_MUTEX_INIT;

static void netman_device_poll_worker(void* data);

static netman_device_packet_receive_hook_t global_rx_hooks[16] = {0};
static sys_mutex_t global_rx_hooks_lock = SYS_MUTEX_INIT;

ferr_t netman_device_register(const uint8_t* mac_address, const netman_device_methods_t* methods, size_t tx_queue_size, netman_device_t** out_device) {
	ferr_t status = ferr_ok;
	netman_device_t* dev = NULL;

	if (!out_device || !mac_address || !methods) {
		status = ferr_invalid_argument;
		goto out_unlocked;
	}

	eve_mutex_lock(&global_device_lock);

	if (global_device) {
		status = ferr_temporary_outage;
		goto out;
	}

	status = sys_mempool_allocate(sizeof(netman_device_t), NULL, (void*)&dev);
	if (status != ferr_ok) {
		goto out;
	}

	global_device = dev;

	simple_memset(dev, 0, sizeof(*dev));

	simple_memcpy(&dev->mac_address[0], mac_address, 6);

	sys_mutex_init(&dev->rx_hooks_lock);
	sys_mutex_init(&dev->tx_pending_lock);

	dev->methods = methods;
	dev->tx_queue_size = tx_queue_size;

	dev->loop = eve_loop_get_main();

	status = sys_mempool_allocate(sizeof(netman_packet_t*) * tx_queue_size, NULL, (void*)&dev->tx_pending);
	if (status != ferr_ok) {
		goto out;
	}

	simple_memset(dev->tx_pending, 0, sizeof(netman_packet_t*) * tx_queue_size);

out:
	sys_mutex_unlock(&global_device_lock);
out_unlocked:
	if (status == ferr_ok) {
		*out_device = dev;
	} else {
		if (dev) {
			if (dev->tx_pending) {
				NETMAN_WUR_IGNORE(sys_mempool_free(dev->tx_pending));
			}
			NETMAN_WUR_IGNORE(sys_mempool_free(dev));
		}
	}
	return status;
};

void netman_device_rx_queue(netman_device_t* device, void* data, size_t data_length, bool end_of_packet, uint8_t checksum) {
	ferr_t status = ferr_ok;

	if (device->rx_drop) {
		status = ferr_unknown;
		goto out;
	} else if (!data) {
		// if data is `NULL`, the device encountered an error and is notifying us so we can drop the packet
		status = ferr_unknown;
		goto out;
	} else {
		if (!device->rx_packet) {
			status = netman_packet_create(&device->rx_packet);
			if (status != ferr_ok) {
				goto out;
			}
		}

		status = netman_packet_append_no_copy(device->rx_packet, data, data_length);
		if (status != ferr_ok) {
			goto out;
		}
	}

out:
	if (status != ferr_ok) {
		if (device->rx_packet) {
			netman_packet_destroy(device->rx_packet);
			device->rx_packet = NULL;
		}

		NETMAN_WUR_IGNORE(sys_page_free(data));
		device->rx_drop = true;
	}

	if (end_of_packet) {
		if (!device->rx_drop) {
			// this means we successfully received the entire packet
			// (because once rx_drop is set, it's only cleared on end-of-packet)

			bool handled = false;

			eve_mutex_lock(&device->rx_hooks_lock);

			for (size_t i = 0; i < sizeof(device->rx_hooks) / sizeof(*device->rx_hooks); ++i) {
				if (!device->rx_hooks[i].hook) {
					continue;
				}

				ferr_t hook_status = device->rx_hooks[i].hook(device->rx_hooks[i].data, device->rx_packet);
				if (hook_status == ferr_ok) {
					handled = true;
					break;
				}
			}

			sys_mutex_unlock(&device->rx_hooks_lock);

			// okay, now let's try global hooks

			eve_mutex_lock(&global_rx_hooks_lock);

			for (size_t i = 0; i < sizeof(global_rx_hooks) / sizeof(*global_rx_hooks); ++i) {
				if (!global_rx_hooks[i].hook) {
					continue;
				}

				ferr_t hook_status = global_rx_hooks[i].hook(global_rx_hooks[i].data, device->rx_packet);
				if (hook_status == ferr_ok) {
					handled = true;
					break;
				}
			}

			sys_mutex_unlock(&global_rx_hooks_lock);

			// if no one handled the packet, destroy it
			if (!handled) {
				netman_packet_destroy(device->rx_packet);
			}

			device->rx_packet = NULL;
		}

		device->rx_drop = false;
	}
};

static void netman_device_poll_worker(void* data) {
	netman_device_t* device = data;

	if (device->methods->rx_poll) {
		device->methods->rx_poll(device);
	}

	if (device->methods->tx_poll) {
		device->methods->tx_poll(device);
	}

	if (device->methods->poll_return) {
		device->methods->poll_return(device);
	}
};

void netman_device_schedule_poll(netman_device_t* device, bool rx, bool tx) {
	NETMAN_WUR_IGNORE(eve_loop_enqueue(device->loop, netman_device_poll_worker, device));
};

static ferr_t netman_device_tx_try_queue(netman_device_t* device);

void netman_device_tx_complete(netman_device_t* device, size_t queue_index) {
	netman_packet_t* packet = NULL;

	eve_mutex_lock(&device->tx_pending_lock);

	packet = device->tx_pending[queue_index];

	if (!packet) {
		sys_mutex_unlock(&device->tx_pending_lock);
		return;
	}

	device->tx_pending[queue_index] = NULL;

	sys_mutex_unlock(&device->tx_pending_lock);

	if (packet->tx_callback) {
		packet->tx_callback(packet->tx_callback_data, ferr_ok);
	}

	netman_packet_destroy(packet);

	// tx_complete being called means the device has free space in its transmit queue,
	// so now try queueing some more buffers
	while (netman_device_tx_try_queue(device) == ferr_ok);
};

static ferr_t netman_device_tx_try_queue(netman_device_t* device) {
	ferr_t status = ferr_ok;

	if (!device->tx_packet) {
		return ferr_temporary_outage;
	}

	netman_packet_buffer_t* buffer = &device->tx_packet->buffers[device->tx_packet_buffer_index];
	bool end_of_packet = device->tx_packet_buffer_index + 1 == device->tx_packet->buffer_count;
	size_t queue_index = 0;

	eve_mutex_lock(&device->tx_pending_lock);

	status = device->methods->tx_queue(device, buffer->address, buffer->length, end_of_packet, &queue_index);
	if (status != ferr_ok) {
		return status;
	}

	// the device now owns the data
	buffer->address = NULL;
	buffer->length = 0;

	if (end_of_packet) {
		device->tx_pending[queue_index] = device->tx_packet;
		device->tx_packet = NULL;
		device->tx_packet_buffer_index = 0;
	} else {
		++device->tx_packet_buffer_index;
	}

	sys_mutex_unlock(&device->tx_pending_lock);

	return status;
};

//
// public API
//

netman_device_t* netman_device_any(void) {
	netman_device_t* dev = NULL;
	eve_mutex_lock(&global_device_lock);
	dev = global_device;
	sys_mutex_unlock(&global_device_lock);
	return dev;
};

ferr_t netman_device_transmit_packet(netman_device_t* device, netman_packet_t* packet, netman_device_transmit_packet_callback_f callback, void* data) {
	ferr_t status = ferr_ok;

	if (device->tx_packet) {
		status = ferr_temporary_outage;
		goto out;
	}

	packet->tx_callback = callback;
	packet->tx_callback_data = data;

	device->tx_packet = packet;
	device->tx_packet_buffer_index = 0;

	// try to queue as many buffers as possible
	while (netman_device_tx_try_queue(device) == ferr_ok);

out:
	return status;
};

ferr_t netman_device_register_packet_receive_hook(netman_device_t* device, netman_device_packet_receive_hook_f hook, void* data) {
	ferr_t status = ferr_ok;

	if (!hook) {
		status = ferr_invalid_argument;
		goto out_unlocked;
	}

	eve_mutex_lock(&device->rx_hooks_lock);

	status = ferr_temporary_outage;
	for (size_t i = 0; i < sizeof(device->rx_hooks) / sizeof(*device->rx_hooks); ++i) {
		if (device->rx_hooks[i].hook) {
			continue;
		}

		status = ferr_ok;
		device->rx_hooks[i].hook = hook;
		device->rx_hooks[i].data = data;
		break;
	}

out:
	sys_mutex_unlock(&device->rx_hooks_lock);
out_unlocked:
	return status;
};

ferr_t netman_device_register_global_packet_receive_hook(netman_device_packet_receive_hook_f hook, void* data) {
	ferr_t status = ferr_ok;

	if (!hook) {
		status = ferr_invalid_argument;
		goto out_unlocked;
	}

	eve_mutex_lock(&global_rx_hooks_lock);

	status = ferr_temporary_outage;
	for (size_t i = 0; i < sizeof(global_rx_hooks) / sizeof(*global_rx_hooks); ++i) {
		if (global_rx_hooks[i].hook) {
			continue;
		}

		status = ferr_ok;
		global_rx_hooks[i].hook = hook;
		global_rx_hooks[i].data = data;
		break;
	}

out:
	sys_mutex_unlock(&global_rx_hooks_lock);
out_unlocked:
	return status;
};

const uint8_t* netman_device_mac_address(netman_device_t* device) {
	return &device->mac_address[0];
};
