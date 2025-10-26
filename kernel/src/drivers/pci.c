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

#include <ferro/core/panic.h>
#include <ferro/core/mempool.h>
#include <ferro/core/ghmap.h>
#include <ferro/core/locks.h>
#include <ferro/core/paging.h>
#include <ferro/core/console.h>
#include <ferro/core/channels.h>
#include <ferro/core/threads.h>
#include <ferro/core/scheduler.h>
#include <ferro/core/workers.h>

#include <libsimple/libsimple.h>

#define MAX_DEVICES_PER_BUS 32
#define MAX_FUNCTIONS_PER_DEVICE 8

fchannel_t* fpci_pciman_client_channel = NULL;

static flock_spin_intsafe_t fpci_tree_lock = FLOCK_SPIN_INTSAFE_INIT;
static simple_ghmap_t fpci_buses;

static const fpci_mcfg_entry_t* fpci_mmio_regions = NULL;
static size_t fpci_mmio_region_count = 0;

static ferr_t fpci_function_lookup_locked(fpci_device_info_t* device, uint8_t function, bool create_if_absent, fpci_function_info_t** out_function);

static const fpci_mcfg_entry_t* fpci_find_entry_for_bus(uint8_t bus) {
	for (size_t i = 0; i < fpci_mmio_region_count; ++i) {
		const fpci_mcfg_entry_t* entry = &fpci_mmio_regions[i];
		if (entry->bus_number_start <= bus && entry->bus_number_end >= bus) {
			return entry;
		}
	}
	return NULL;
};

FERRO_ALWAYS_INLINE void* fpci_function_physical_address(const fpci_mcfg_entry_t* entry, uint8_t bus, uint8_t device, uint8_t function) {
	return (void*)(entry->base_address + (((bus - entry->bus_number_start) << 20ull) | (device << 15ull) | (function << 12ull)));
};

ferr_t fpci_bus_lookup(uint8_t bus, bool create_if_absent, fpci_bus_info_t** out_bus) {
	ferr_t status = ferr_ok;
	bool created = false;
	fpci_bus_info_t* info = NULL;

	flock_spin_intsafe_lock(&fpci_tree_lock);

	status = simple_ghmap_lookup_h(&fpci_buses, bus, create_if_absent, sizeof(fpci_bus_info_t), &created, (void*)&info, NULL);
	if (status != ferr_ok) {
		goto out;
	}

	if (created) {
		info->location = bus;

		info->mcfg_entry = fpci_find_entry_for_bus(bus);
		if (!info->mcfg_entry) {
			FERRO_WUR_IGNORE(simple_ghmap_clear_h(&fpci_buses, bus));
			goto out;
		}

		status = simple_ghmap_init(&info->devices, 0, 0, simple_ghmap_allocate_mempool, simple_ghmap_free_mempool, NULL, NULL, NULL, NULL, NULL, NULL);
		if (status != ferr_ok) {
			FERRO_WUR_IGNORE(simple_ghmap_clear_h(&fpci_buses, bus));
			goto out;
		}
	}

out:
	flock_spin_intsafe_unlock(&fpci_tree_lock);
out_unlocked:
	if (status == ferr_ok) {
		if (out_bus) {
			*out_bus = info;
		}
	}
	return status;
};

ferr_t fpci_device_lookup(fpci_bus_info_t* bus, uint8_t device, bool create_if_absent, fpci_device_info_t** out_device) {
	ferr_t status = ferr_ok;
	bool created = false;
	fpci_device_info_t* info = NULL;

	flock_spin_intsafe_lock(&fpci_tree_lock);

	status = simple_ghmap_lookup_h(&bus->devices, device, create_if_absent, sizeof(fpci_device_info_t), &created, (void*)&info, NULL);
	if (status != ferr_ok) {
		goto out;
	}

	if (created) {
		info->bus = bus;
		info->location = device;

		// initial size is 1 because we're guaranteed to have at least 1 function
		status = simple_ghmap_init(&info->functions, 1, 0, simple_ghmap_allocate_mempool, simple_ghmap_free_mempool, NULL, NULL, NULL, NULL, NULL, NULL);
		if (status != ferr_ok) {
			FERRO_WUR_IGNORE(simple_ghmap_clear_h(&bus->devices, device));
			goto out;
		}

		status = fpci_function_lookup_locked(info, 0, true, &info->function0);
		if (status != ferr_ok) {
			// function doesn't exist (and therefore neither does the device)
			simple_ghmap_destroy(&info->functions);
			FERRO_WUR_IGNORE(simple_ghmap_clear_h(&bus->devices, device));
			goto out;
		}

		// at this point, we definitely have a valid device
	}

out:
	flock_spin_intsafe_unlock(&fpci_tree_lock);
out_unlocked:
	if (status == ferr_ok) {
		if (out_device) {
			*out_device = info;
		}
	}
	return status;
};

static ferr_t fpci_function_lookup_locked(fpci_device_info_t* device, uint8_t function, bool create_if_absent, fpci_function_info_t** out_function) {
	ferr_t status = ferr_ok;
	bool created = false;
	fpci_function_info_t* info = NULL;

	status = simple_ghmap_lookup_h(&device->functions, function, create_if_absent, sizeof(fpci_function_info_t), &created, (void*)&info, NULL);
	if (status != ferr_ok) {
		goto out;
	}

	if (created) {
		info->device = device;
		info->location = function;

		info->capabilities = NULL;
		info->capability_count = 0;

		simple_memset(&info->bars[0], 0, sizeof(info->bars));

		info->handler.handler = NULL;
		info->handler.data = NULL;
		info->handler.setup = false;

		flock_spin_intsafe_init(&info->lock);

		void* phys = fpci_function_physical_address(info->device->bus->mcfg_entry, info->device->bus->location, info->device->location, function);

		status = fpage_map_kernel_any(phys, 1, (void*)&info->mmio_base, fpage_flag_no_cache);
		if (status != ferr_ok) {
			FERRO_WUR_IGNORE(simple_ghmap_clear_h(&device->functions, function));
			goto out;
		}

		if (info->mmio_base[0] == 0xffffffffu) {
			// function does not exist
			status = ferr_no_such_resource;
			FERRO_WUR_IGNORE(fpage_unmap_kernel((void*)info->mmio_base, 1));
			FERRO_WUR_IGNORE(simple_ghmap_clear_h(&device->functions, function));
			goto out;
		}

		uint32_t tmp = info->mmio_base[0];
		info->public.vendor_id = tmp & 0xffffu;
		info->public.device_id = tmp >> 16;

		tmp = info->mmio_base[2];
		info->public.class_code = tmp >> 24;
		info->public.subclass_code = (tmp >> 16) & 0xff;
		info->public.programming_interface = (tmp >> 8) & 0xff;

		// populate capabilities
		if ((info->mmio_base[1] & (1 << 20)) != 0) {
			uint8_t offset = info->mmio_base[13] & 0xff;
			uint8_t index = offset / sizeof(uint32_t);

			while (index != 0) {
				volatile uint32_t* base = &info->mmio_base[index];

				++info->capability_count;

				offset = (base[0] >> 8) & 0xff;
				index = offset / sizeof(uint32_t);
			}

			status = fmempool_allocate(sizeof(fpci_capability_info_t) * info->capability_count, NULL, (void*)&info->capabilities);
			if (status != ferr_ok) {
				FERRO_WUR_IGNORE(fpage_unmap_kernel((void*)info->mmio_base, 1));
				FERRO_WUR_IGNORE(simple_ghmap_clear_h(&device->functions, function));
				goto out;
			}

			offset = info->mmio_base[13] & 0xff;
			index = offset / sizeof(uint32_t);

			size_t i = 0;

			while (index != 0) {
				volatile uint32_t* base = &info->mmio_base[index];
				uint8_t id = base[0] & 0xff;
				fpci_capability_info_t* cap = &info->capabilities[i++];

				cap->id = id;
				cap->function = info;
				cap->mmio_base = base;

				offset = (base[0] >> 8) & 0xff;
				index = offset / sizeof(uint32_t);
			}
		}
	}

out:
	if (status == ferr_ok) {
		if (out_function) {
			*out_function = info;
		}
	}
	return status;
};

ferr_t fpci_function_lookup(fpci_device_info_t* device, uint8_t function, bool create_if_absent, fpci_function_info_t** out_function) {
	ferr_t status;
	flock_spin_intsafe_lock(&fpci_tree_lock);
	status = fpci_function_lookup_locked(device, function, create_if_absent, out_function);
	flock_spin_intsafe_unlock(&fpci_tree_lock);
	return status;
};

ferr_t fpci_bus_scan(fpci_bus_info_t* bus) {
	ferr_t status = ferr_ok;

	for (uint8_t i = 0; i < MAX_DEVICES_PER_BUS; ++i) {
		fpci_device_info_t* device = NULL;

		status = fpci_device_lookup(bus, i, true, &device);
		if (status == ferr_no_such_resource) {
			status = ferr_ok;
			continue;
		} else if (status != ferr_ok) {
			goto out;
		}

		status = fpci_device_scan(device);
		if (status != ferr_ok) {
			goto out;
		}
	}

out:
	return status;
};

ferr_t fpci_device_scan(fpci_device_info_t* device) {
	ferr_t status = ferr_ok;

	status = fpci_function_scan(device->function0);
	if (status != ferr_ok) {
		goto out;
	}

	uint8_t header_type = (device->function0->mmio_base[3] >> 16) & 0xffu;

	if ((header_type & (1 << 7)) != 0) {
		// this device has multiple functions
		for (uint8_t i = 1; i < MAX_FUNCTIONS_PER_DEVICE; ++i) {
			fpci_function_info_t* function = NULL;

			status = fpci_function_lookup(device, i, true, &function);
			if (status == ferr_no_such_resource) {
				status = ferr_ok;
				continue;
			} else if (status != ferr_ok) {
				goto out;
			}

			status = fpci_function_scan(function);
			if (status != ferr_ok) {
				goto out;
			}
		}
	}

out:
	return status;
};

ferr_t fpci_function_scan(fpci_function_info_t* function) {
	ferr_t status = ferr_ok;

	//
	// detect BARs
	//

	flock_spin_intsafe_lock(&function->lock);

	uint8_t valid_bar_index = 0;

	for (uint8_t bar_index = 0; bar_index < 6; ++bar_index) {
		bool is_64_bit = false;
		fpci_bar_t* bar = &function->bars[valid_bar_index];
		uintptr_t physical_base;
		uint32_t orig_bar;
		uint32_t orig_bar2;
		uint64_t size;

		if ((function->mmio_base[4 + bar_index] & 1) != 0) {
			// this is an I/O BAR
			// TODO
			bar->type = fpci_bar_type_io;
			bar->raw_index = bar_index;

			fconsole_logf("PCI: %02x:%02x.%x: found I/O BAR%u (real index = %u) at TODO\n", function->device->bus->location, function->device->location, function->location, valid_bar_index, bar_index);

			++valid_bar_index;
		} else {
			// this is a memory BAR

			if (((function->mmio_base[4 + bar_index] >> 1) & 0x3) == 0x02) {
				is_64_bit = true;
			}

			physical_base = function->mmio_base[4 + bar_index] & 0xfffffff0;

			if (is_64_bit) {
				physical_base |= (uint64_t)(function->mmio_base[4 + bar_index + 1]) << 32;
			}

			if (physical_base == 0) {
				// this is not a valid BAR
				continue;
			}

			// disable memory access for the device
			function->mmio_base[1] &= ~(uint32_t)(1 << 1);

			// save the original BAR values
			orig_bar = function->mmio_base[4 + bar_index];
			orig_bar2 = 0;

			if (is_64_bit) {
				orig_bar2 = function->mmio_base[4 + bar_index + 1];
			}

			// fill in the BAR with all 1's
			function->mmio_base[4 + bar_index] = 0xffffffff;
			if (is_64_bit) {
				function->mmio_base[4 + bar_index + 1] = 0xffffffff;
			}

			size = function->mmio_base[4 + bar_index] & 0xfffffff0;
			if (is_64_bit) {
				size |= (uint64_t)(function->mmio_base[4 + bar_index + 1]) << 32;
			} else {
				size |= 0xffffffffull << 32;
			}

			size = ~size + 1;

			// restore the BAR values
			function->mmio_base[4 + bar_index] = orig_bar;
			if (is_64_bit) {
				function->mmio_base[4 + bar_index + 1] = orig_bar2;
			}

			// re-enable memory access for the device
			function->mmio_base[1] |= (uint32_t)(1 << 1);

			bar->type = fpci_bar_type_memory;
			bar->raw_index = bar_index;
			bar->physical_base = physical_base;
			bar->size = size;

			fconsole_logf("PCI: %02x:%02x.%x: found memory BAR%u (real index = %u) at %p (" FERRO_U64_FORMAT " bytes)\n", function->device->bus->location, function->device->location, function->location, valid_bar_index, bar_index, (void*)physical_base, size);

			if (is_64_bit) {
				// we used another BAR, so skip it
				++bar_index;
			}

			++valid_bar_index;
		}
	}

	flock_spin_intsafe_unlock(&function->lock);

	//
	// detect additional PCI devices if this is a PCI bus/bridge
	//

	if (
		function->public.class_code == 0x06 && function->public.subclass_code == 0x04 &&

		// the host controller is a special case that is handled by the PCI initialization code
		!(function->device->bus->location == 0 && function->device->location == 0)
	) {
		uint8_t secondary_bus_number = (function->mmio_base[6] >> 8) & 0xff;
		fpci_bus_info_t* secondary_bus = NULL;

		status = fpci_bus_lookup(secondary_bus_number, true, &secondary_bus);
		if (status == ferr_no_such_resource) {
			status = ferr_ok;
			fconsole_logf("Warning: failed to lookup secondary bus (%u) for %02x:%02x.%x\n", secondary_bus_number, function->device->bus->location, function->device->location, function->location);
			goto out;
		}

		status = fpci_bus_scan(secondary_bus);
		if (status != ferr_ok) {
			goto out;
		}
	}

out:
	return status;
};

ferr_t fpci_function_register_interrupt_handler(fpci_function_info_t* function, fpci_device_interrupt_handler_f handler, void *data) {
	ferr_t status = ferr_ok;

	if (function->handler.handler) {
		status = ferr_already_in_progress;
		goto out;
	}

	if (function->handler.setup) {
		function->handler.handler = handler;
		function->handler.data = data;
		goto out;
	}

	for (size_t i = 0; i < function->capability_count; ++i) {
		fpci_capability_info_t* cap = &function->capabilities[i];

		if (cap->id == fpci_capability_id_msi) {
			function->handler.handler = handler;
			function->handler.data = data;
			function->handler.setup = true;

			status = farch_pci_function_register_msi_handler(cap);
			if (status != ferr_ok) {
				function->handler.handler = NULL;
				function->handler.data = NULL;
				function->handler.setup = false;
				goto out;
			}

			// enable MSI now
			cap->mmio_base[0] |= 1 << 16;

			goto out;
		} else if (cap->id == fpci_capability_id_msi_x) {
			volatile uint32_t* table_bar = NULL;
			volatile fpci_msi_x_entry_t* table = NULL;
			size_t table_entry_count = 0;

			status = fpci_device_get_mapped_bar_raw_index((void*)function, cap->mmio_base[1] & 3, &table_bar, NULL);
			if (status != ferr_ok) {
				goto out;
			}

			table = (void*)((char*)table_bar + (cap->mmio_base[1] & ~3));
			table_entry_count = ((cap->mmio_base[0] >> 16) & 0x7ff) + 1;

			function->handler.handler = handler;
			function->handler.data = data;
			function->handler.setup = true;

			status = farch_pci_function_register_msi_x_handler(function, table, table_entry_count);
			if (status != ferr_ok) {
				function->handler.handler = NULL;
				function->handler.data = NULL;
				function->handler.setup = false;
				goto out;
			}

			// enable MSI-X now
			cap->mmio_base[0] |= 1 << 31;

			goto out;
		}
	}

	status = ferr_unsupported;

out:
	return status;
};

static bool fpci_root_bus_function_iterator(void* context, simple_ghmap_t* hashmap, simple_ghmap_hash_t hash, const void* key, size_t key_size, void* entry, size_t entry_size) {
	fpci_function_info_t* function = entry;
	ferr_t status = ferr_ok;

	if (function->location == 0) {
		// we've already scanned bus 0
		return true;
	}

	// the index of this function on the root device is the bus number it controls
	fpci_bus_info_t* bus = NULL;
	status = fpci_bus_lookup(function->location, true, &bus);
	if (status != ferr_ok) {
		fconsole_logf("Warning: failed to lookup bus (%u) for %02x:%02x.%x\n", function->location, function->device->bus->location, function->device->location, function->location);
		return true;
	}

	fpanic_status(fpci_bus_scan(bus));

	return true;
};

static bool fpci_debug_function_iterator(void* context, simple_ghmap_t* hashmap, simple_ghmap_hash_t hash, const void* key, size_t key_size, void* entry, size_t entry_size) {
	fpci_function_info_t* function = entry;
	fconsole_logf("Found %02x:%02x.%x (VID = 0x%04x, DID = 0x%04x, class code = 0x%02x, subclass code = 0x%02x, programming interface = 0x%02x)\n", function->device->bus->location, function->device->location, function->location, function->public.vendor_id, function->public.device_id, function->public.class_code, function->public.subclass_code, function->public.programming_interface);
	for (size_t i = 0; i < function->capability_count; ++i) {
		fconsole_logf("  Capability: 0x%02x\n", function->capabilities[i].id);
	}
	return true;
};

static bool fpci_debug_device_iterator(void* context, simple_ghmap_t* hashmap, simple_ghmap_hash_t hash, const void* key, size_t key_size, void* entry, size_t entry_size) {
	fpci_device_info_t* device = entry;
	simple_ghmap_for_each(&device->functions, fpci_debug_function_iterator, NULL);
	return true;
};

static bool fpci_debug_bus_iterator(void* context, simple_ghmap_t* hashmap, simple_ghmap_hash_t hash, const void* key, size_t key_size, void* entry, size_t entry_size) {
	fpci_bus_info_t* bus = entry;
	simple_ghmap_for_each(&bus->devices, fpci_debug_device_iterator, NULL);
	return true;
};

#if 0
	#define pciman_debug_f(msg, ...) fconsole_logf("pciman: " msg "\n", ## __VA_ARGS__)
#else
	#define pciman_debug_f(...)
#endif

FERRO_STRUCT_FWD(pciman_wait_context);

FERRO_STRUCT(pciman_client_context) {
	pciman_client_context_t** prev;
	pciman_client_context_t* next;
	pciman_wait_context_t* wait_context;
	fchannel_t* client;
	fwaitq_waiter_t death_waiter;
	fwaitq_waiter_t message_arrival_waiter;
	fpci_function_info_t* registered_function;
	bool dead;
	bool interrupt_pending;
	uint64_t read_value;
	volatile void* read_on_interrupt_address;
	uint8_t read_on_interrupt_size;
	volatile void* write_on_interrupt_address;
	uint8_t write_on_interrupt_size;
	uint64_t write_on_interrupt_data;
};

FERRO_STRUCT(pciman_wait_context) {
	flock_semaphore_t sema;
	fwaitq_waiter_t client_arrival_waiter;
	fchannel_t* server;
	flock_mutex_t clients_mutex;
	pciman_client_context_t* clients;
};

static void pciman_client_arrival(void* data) {
	pciman_wait_context_t* wait_context = data;
	flock_semaphore_up(&wait_context->sema);
	fwaitq_wait(&wait_context->server->message_arrival_waitq, &wait_context->client_arrival_waiter);
};

static void pciman_client_death(void* data) {
	pciman_client_context_t* client_context = data;
	client_context->dead = true;
	flock_semaphore_up(&client_context->wait_context->sema);
};

static void pciman_message_arrival(void* data) {
	pciman_client_context_t* client_context = data;
	pciman_debug_f("got message");
	flock_semaphore_up(&client_context->wait_context->sema);
	fwaitq_wait(&client_context->client->message_arrival_waitq, &client_context->message_arrival_waiter);
};

static bool pciman_scan_size_function_iterator(void* context, simple_ghmap_t* hashmap, simple_ghmap_hash_t hash, const void* key, size_t key_size, void* entry, size_t entry_size) {
	size_t* size_ptr = context;
	fpci_function_info_t* info = entry;
	*size_ptr += sizeof(fpci_device_t);
	return true;
};

static bool pciman_scan_size_device_iterator(void* context, simple_ghmap_t* hashmap, simple_ghmap_hash_t hash, const void* key, size_t key_size, void* entry, size_t entry_size) {
	fpci_device_info_t* info = entry;
	return simple_ghmap_for_each(&info->functions, pciman_scan_size_function_iterator, context) == ferr_ok;
};

static bool pciman_scan_size_bus_iterator(void* context, simple_ghmap_t* hashmap, simple_ghmap_hash_t hash, const void* key, size_t key_size, void* entry, size_t entry_size) {
	fpci_bus_info_t* info = entry;
	return simple_ghmap_for_each(&info->devices, pciman_scan_size_device_iterator, context) == ferr_ok;
};

static bool pciman_scan_function_iterator(void* context, simple_ghmap_t* hashmap, simple_ghmap_hash_t hash, const void* key, size_t key_size, void* entry, size_t entry_size) {
	fpci_device_t** ptr = context;
	fpci_function_info_t* info = entry;
	fpci_device_t* curr = *ptr;
	simple_memcpy(curr, &info->public, sizeof(*curr));
	*ptr += 1;
	return true;
};

static bool pciman_scan_device_iterator(void* context, simple_ghmap_t* hashmap, simple_ghmap_hash_t hash, const void* key, size_t key_size, void* entry, size_t entry_size) {
	fpci_device_info_t* info = entry;
	return simple_ghmap_for_each(&info->functions, pciman_scan_function_iterator, context) == ferr_ok;
};

static bool pciman_scan_bus_iterator(void* context, simple_ghmap_t* hashmap, simple_ghmap_hash_t hash, const void* key, size_t key_size, void* entry, size_t entry_size) {
	fpci_bus_info_t* info = entry;
	return simple_ghmap_for_each(&info->devices, pciman_scan_device_iterator, context) == ferr_ok;
};

static void pciman_client_interrupt_handler(void* data) {
	pciman_client_context_t* client_context = data;
#if 0
	fchannel_message_t message;

	message.conversation_id = fchannel_conversation_id_none;
	// message id is assigned by the channel
	message.attachments = NULL;
	message.attachments_length = 0;
	message.body = NULL;
	message.body_length = 0;

	if (fchannel_send(client_context->client, fchannel_send_flag_no_wait, &message) != ferr_ok) {
		fchannel_message_destroy(&message);
#endif

		// set a flag and let the pciman event loop handle this instead
		// (note that this will result in coalesced interrupts)
		client_context->interrupt_pending = true;

		if (client_context->read_on_interrupt_address) {
			switch (client_context->read_on_interrupt_size) {
				case 1:
					client_context->read_value = *(volatile uint8_t*)client_context->read_on_interrupt_address;
					break;
				case 2:
					client_context->read_value = *(volatile uint16_t*)client_context->read_on_interrupt_address;
					break;
				case 4:
					client_context->read_value = *(volatile uint32_t*)client_context->read_on_interrupt_address;
					break;
				case 8:
					client_context->read_value = *(volatile uint64_t*)client_context->read_on_interrupt_address;
					break;
			}
		}

		if (client_context->write_on_interrupt_address) {
			switch (client_context->write_on_interrupt_size) {
				case 1:
					*(volatile uint8_t*)client_context->write_on_interrupt_address = (uint8_t)(client_context->write_on_interrupt_data & 0xff);
					break;
				case 2:
					*(volatile uint16_t*)client_context->write_on_interrupt_address = (uint16_t)(client_context->write_on_interrupt_data & 0xffff);
					break;
				case 4:
					*(volatile uint32_t*)client_context->write_on_interrupt_address = (uint32_t)(client_context->write_on_interrupt_data & 0xffffffff);
					break;
				case 8:
					*(volatile uint64_t*)client_context->write_on_interrupt_address = client_context->write_on_interrupt_data;
					break;
			}
		}

		flock_semaphore_up(&client_context->wait_context->sema);
#if 0
	}
#endif
};

FERRO_PACKED_STRUCT(pciman_device_registration_request) {
	uint8_t message_type;
	uint16_t vendor_id;
	uint16_t device_id;
};

FERRO_PACKED_STRUCT(pciman_read_on_interrupt_request) {
	uint8_t message_id;
	uint8_t bar_index;
	uint8_t size;
	uint64_t offset;
};

FERRO_PACKED_STRUCT(pciman_write_on_interrupt_request) {
	uint8_t message_id;
	uint8_t bar_index;
	uint8_t size;
	uint64_t offset;
	uint64_t data;
};

FERRO_PACKED_STRUCT(pciman_get_mapped_bar_request) {
	uint8_t message_id;
	uint8_t bar_index;
};

FERRO_PACKED_STRUCT(pciman_get_mapped_bar_response) {
	ferr_t status;
	uint64_t bar_size;
};

FERRO_PACKED_STRUCT(pciman_config_space_read_request) {
	uint8_t message_type;
	uint64_t offset;
	uint8_t size;
};

FERRO_PACKED_STRUCT(pciman_config_space_read_response) {
	ferr_t status;
	char data[];
};

FERRO_PACKED_STRUCT(pciman_config_space_write_request) {
	uint8_t message_type;
	uint64_t offset;
	uint8_t size;
	char data[];
};

static void pciman_thread_entry(void* data) {
	fchannel_t* server = NULL;
	pciman_wait_context_t wait_context;

	flock_semaphore_init(&wait_context.sema, 0);
	fwaitq_waiter_init(&wait_context.client_arrival_waiter, pciman_client_arrival, &wait_context);
	flock_mutex_init(&wait_context.clients_mutex);
	wait_context.clients = NULL;

	fpanic_status(fchannel_new_pair(&server, &fpci_pciman_client_channel));

	wait_context.server = server;
	fwaitq_wait(&server->message_arrival_waitq, &wait_context.client_arrival_waiter);

	while (true) {
		// wait for new events
		flock_semaphore_down(&wait_context.sema);

		flock_mutex_lock(&wait_context.clients_mutex);

		while (true) {
			fchannel_message_t message;
			fchannel_t* client = NULL;
			pciman_client_context_t* client_context = NULL;
			ferr_t status = fchannel_receive(server, fchannel_receive_flag_no_wait, &message);
			fchannel_message_attachment_channel_t* channel_attachment = NULL;

			if (status != ferr_ok) {
				pciman_debug_f("spurious wakeup");
				break;
			}

			if (message.attachments_length < sizeof(fchannel_message_attachment_channel_t)) {
				pciman_debug_f("bad message: attachments too short (%lu)", message.attachments_length);
				fchannel_message_destroy(&message);
				continue;
			}

			channel_attachment = (void*)message.attachments;
			if (channel_attachment->header.type != fchannel_message_attachment_type_channel) {
				pciman_debug_f("bad message: attachment not a channel (%d)", channel_attachment->header.type);
				fchannel_message_destroy(&message);
				continue;
			}

			// detach the client channel from the message and destroy the message
			client = channel_attachment->channel;
			channel_attachment->channel = NULL;
			fchannel_message_destroy(&message);

			if (fmempool_allocate(sizeof(*client_context), NULL, (void*)&client_context) != ferr_ok) {
				pciman_debug_f("failed to allocate client context");
				FERRO_WUR_IGNORE(fchannel_close(client));
				fchannel_release(client);
			}

			pciman_debug_f("new client");

			client_context->prev = &wait_context.clients;
			client_context->next = *client_context->prev;
			client_context->wait_context = &wait_context;
			client_context->client = client;
			fwaitq_waiter_init(&client_context->death_waiter, pciman_client_death, client_context);
			fwaitq_waiter_init(&client_context->message_arrival_waiter, pciman_message_arrival, client_context);
			client_context->registered_function = NULL;
			client_context->dead = false;
			client_context->interrupt_pending = false;
			client_context->read_value = 0;
			client_context->read_on_interrupt_address = NULL;
			client_context->read_on_interrupt_size = 0;
			client_context->write_on_interrupt_address = NULL;
			client_context->write_on_interrupt_size = 0;
			client_context->write_on_interrupt_data = 0;

			if (client_context->next) {
				client_context->next->prev = &client_context->next;
			}
			*client_context->prev = client_context;

			fwaitq_wait(&fchannel_peer(client, false)->close_waitq, &client_context->death_waiter);
			fwaitq_wait(&client->message_arrival_waitq, &client_context->message_arrival_waiter);
		}

		pciman_client_context_t* next = NULL;
		for (pciman_client_context_t* client_context = wait_context.clients; client_context != NULL; client_context = next) {
			next = client_context->next;

			if (client_context->dead) {
				// clear out this client

				pciman_debug_f("client died");

				// first, unlink it
				*client_context->prev = client_context->next;
				if (client_context->next) {
					client_context->next->prev = client_context->prev;
				}

				// now unregister it (if necessary)
				if (client_context->registered_function) {
					client_context->registered_function->handler.handler = NULL;
					client_context->registered_function->handler.data = NULL;
				}

				// now unwait the waiters
				fwaitq_unwait(&fchannel_peer(client_context->client, false)->close_waitq, &client_context->death_waiter);
				fwaitq_unwait(&client_context->client->message_arrival_waitq, &client_context->message_arrival_waiter);

				// now release our resources
				FERRO_WUR_IGNORE(fchannel_close(client_context->client));
				fchannel_release(client_context->client);
				FERRO_WUR_IGNORE(fmempool_free(client_context));

				continue;
			}

			// check if we need to send an interrupt message
			if (client_context->interrupt_pending) {
				fchannel_message_t message;
				ferr_t status = ferr_ok;

				pciman_debug_f("sending interrupt message");

				message.conversation_id = fchannel_conversation_id_none;
				// message id is assigned by the channel
				message.attachments = NULL;
				message.attachments_length = 0;
				message.body_length = sizeof(uint64_t);
				message.body = NULL;

				status = fmempool_allocate(message.body_length, NULL, &message.body);
				if (status != ferr_ok) {
					// leave the interrupt flag pending
				} else {
					*(uint64_t*)message.body = client_context->read_value;

					if (fchannel_send(client_context->client, fchannel_send_flag_no_wait, &message) != ferr_ok) {
						fchannel_message_destroy(&message);

						// leave the interrupt pending flag
					} else {
						// we successfully sent the message, so clear the interrupt pending flag
						client_context->interrupt_pending = false;
					}
				}
			}

			// check if we've got a new message from this client
			// (we only check for one new message from each client on every event loop pass to try to round-robin it and balance it out)

			fchannel_message_t message;
			ferr_t status = ferr_invalid_argument;

			if (fchannel_receive(client_context->client, fchannel_receive_flag_no_wait, &message) != ferr_ok) {
				continue;
			}

			// we did get a message from this client, so schedule another event loop pass to check it again for more messages
			flock_semaphore_up(&wait_context.sema);

			// okay, let's see what the message is about

			fchannel_message_t outgoing_message;

			outgoing_message.conversation_id = message.conversation_id;
			// message id is assigned by the channel
			outgoing_message.attachments = NULL;
			outgoing_message.attachments_length = 0;
			outgoing_message.body_length = 0;
			outgoing_message.body = NULL;

			uint8_t message_id = (message.body_length < 1) ? 0 : *((uint8_t*)message.body);

			if (message.body_length < 1) {
				// discard it and respond with "invalid argument"
				pciman_debug_f("discarding invalid message");
			} else if (message_id == 1) {
				// tree query

				pciman_debug_f("tree query");

				// TODO: maybe support more complex tree queries.
				//       for now, we just send back the entire tree.

				flock_spin_intsafe_lock(&fpci_tree_lock);
				status = simple_ghmap_for_each(&fpci_buses, pciman_scan_size_bus_iterator, &outgoing_message.body_length);
				if (status == ferr_ok) {
					outgoing_message.body_length += sizeof(ferr_t);
					status = fmempool_allocate(outgoing_message.body_length, NULL, &outgoing_message.body);
					if (status == ferr_ok) {
						fpci_device_t* curr = outgoing_message.body + sizeof(ferr_t);
						status = simple_ghmap_for_each(&fpci_buses, pciman_scan_bus_iterator, &curr);
					}
				}
				flock_spin_intsafe_unlock(&fpci_tree_lock);
			} else if (message_id == 2) {
				// device registration
				pciman_device_registration_request_t* request_body = message.body;

				pciman_debug_f("device registration");

				fpci_device_t* device = NULL;

				status = (client_context->registered_function) ? ferr_already_in_progress : ferr_ok;
				if (status == ferr_ok) {
					status = fpci_lookup(request_body->vendor_id, request_body->device_id, &device);
					if (status == ferr_ok) {
						client_context->registered_function = (void*)device;
					}
				}
			} else if (message_id == 3) {
				// interrupt registration

				pciman_debug_f("interrupt registration");

				status = (client_context->registered_function) ? ferr_ok : ferr_no_such_resource;
				if (status == ferr_ok) {
					status = fpci_device_register_interrupt_handler((void*)client_context->registered_function, pciman_client_interrupt_handler, client_context);
				}
			} else if (message_id == 4) {
				// get mapped bar
				pciman_get_mapped_bar_request_t* request_body = message.body;

				pciman_debug_f("get mapped bar");

				status = (client_context->registered_function) ? ferr_ok : ferr_no_such_resource;
				if (status == ferr_ok) {
					outgoing_message.body_length = sizeof(ferr_t) + sizeof(uint64_t);
					status = fmempool_allocate(outgoing_message.body_length, NULL, (void*)&outgoing_message.body);
					if (status == ferr_ok) {
						pciman_get_mapped_bar_response_t* response_body = outgoing_message.body;
						outgoing_message.attachments_length = sizeof(fchannel_message_attachment_mapping_t);
						status = fmempool_allocate(outgoing_message.attachments_length, NULL, (void*)&outgoing_message.attachments);
						if (status == ferr_ok) {
							size_t bar_size = 0;
							fchannel_message_attachment_mapping_t* mapping_attachment = (void*)outgoing_message.attachments;
							mapping_attachment->header.next_offset = 0;
							mapping_attachment->header.length = sizeof(*mapping_attachment);
							mapping_attachment->header.type = fchannel_message_attachment_type_mapping;
							mapping_attachment->mapping = NULL;
							status = fpci_device_get_mapped_bar_mapping(&client_context->registered_function->public, request_body->bar_index, &mapping_attachment->mapping, &bar_size);
							if (status == ferr_ok) {
								response_body->bar_size = bar_size;
							}
						}
					}
				}
			} else if (message_id == 5) {
				// enable bus mastering

				pciman_debug_f("enable bus mastering");

				status = (client_context->registered_function) ? ferr_ok : ferr_no_such_resource;
				if (status == ferr_ok) {
					status = fpci_device_enable_bus_mastering(&client_context->registered_function->public);
				}
			} else if (message_id == 6) {
				// read config space
				pciman_config_space_read_request_t* request_body = message.body;

				pciman_debug_f("read config space");

				status = (client_context->registered_function) ? ferr_ok : ferr_no_such_resource;
				if (status == ferr_ok) {
					outgoing_message.body_length = request_body->size + sizeof(ferr_t);
					status = fmempool_allocate(outgoing_message.body_length, NULL, &outgoing_message.body);
					if (status == ferr_ok) {
						pciman_config_space_read_response_t* response_body = outgoing_message.body;
						status = fpci_device_config_space_read(&client_context->registered_function->public, request_body->offset, request_body->size, response_body->data);
					}
				}
			} else if (message_id == 7) {
				// write config space
				pciman_config_space_write_request_t* request_body = message.body;

				pciman_debug_f("write config space");

				status = (client_context->registered_function) ? ferr_ok : ferr_no_such_resource;
				if (status == ferr_ok) {
					status = fpci_device_config_space_write(&client_context->registered_function->public, request_body->offset, request_body->size, request_body->data);
				}
			} else if (message_id == 8) {
				// read on interrupt
				pciman_read_on_interrupt_request_t* request_body = message.body;

				pciman_debug_f("read on interrupt");

				switch (request_body->size) {
					case 1:
					case 2:
					case 4:
					case 8:
						status = ferr_ok;
						break;
					default:
						status = ferr_invalid_argument;
				}

				if (status == ferr_ok) {
					status = (client_context->registered_function) ? ferr_ok : ferr_no_such_resource;
					if (status == ferr_ok) {
						volatile uint32_t* tmp = NULL;
						size_t size = 0;
						status = fpci_device_get_mapped_bar(&client_context->registered_function->public, request_body->bar_index, &tmp, &size);
						if (status == ferr_ok) {
							if (request_body->offset >= size || (request_body->offset + request_body->size) > size) {
								status = ferr_invalid_argument;
							} else {
								client_context->read_on_interrupt_size = request_body->size;
								client_context->read_on_interrupt_address = (volatile uint8_t*)tmp + request_body->offset;
							}
						}
					}
				}
			} else if (message_id == 9) {
				// write on interrupt
				pciman_write_on_interrupt_request_t* request_body = message.body;

				pciman_debug_f("write on interrupt");

				switch (request_body->size) {
					case 1:
					case 2:
					case 4:
					case 8:
						status = ferr_ok;
						break;
					default:
						status = ferr_invalid_argument;
				}

				if (status == ferr_ok) {
					status = (client_context->registered_function) ? ferr_ok : ferr_no_such_resource;
					if (status == ferr_ok) {
						volatile uint32_t* tmp = NULL;
						size_t size = 0;
						status = fpci_device_get_mapped_bar(&client_context->registered_function->public, request_body->bar_index, &tmp, &size);
						if (status == ferr_ok) {
							if (request_body->offset >= size || (request_body->offset + request_body->size) > size) {
								status = ferr_invalid_argument;
							} else {
								client_context->write_on_interrupt_size = request_body->size;
								client_context->write_on_interrupt_data = request_body->data;
								client_context->write_on_interrupt_address = (volatile uint8_t*)tmp + request_body->offset;
							}
						}
					}
				}
			}

			if (status != ferr_ok) {
				fchannel_message_destroy(&outgoing_message);
				outgoing_message.body = NULL;
				outgoing_message.body_length = 0;
				outgoing_message.attachments = NULL;
				outgoing_message.attachments_length = 0;
			}

			if (!outgoing_message.body) {
				fpanic_status(fmempool_allocate(sizeof(ferr_t), NULL, &outgoing_message.body));
				outgoing_message.body_length = sizeof(ferr_t);
			}

			*(ferr_t*)outgoing_message.body = status;

			if (fchannel_send(client_context->client, fchannel_send_flag_no_wait, &outgoing_message) != ferr_ok) {
				fchannel_message_destroy(&outgoing_message);
			}

			fchannel_message_destroy(&message);
		}

		flock_mutex_unlock(&wait_context.clients_mutex);
	}
};

void fpci_init(void) {
	ferr_t status = ferr_ok;

	// initial size is 1 because it's very likely we have at least 1 bus
	fpanic_status(simple_ghmap_init(&fpci_buses, 1, 0, simple_ghmap_allocate_mempool, simple_ghmap_free_mempool, NULL, NULL, NULL, NULL, NULL, NULL));

	fpci_mcfg_t* table = (void*)facpi_find_table("MCFG");
	if (!table) {
		fconsole_log("Warning: no MCFG table found; no PCI devices will be available\n");
		return;
	}

	fpci_mmio_regions = table->entries;
	fpci_mmio_region_count = (table->header.length - offsetof(fpci_mcfg_t, entries)) / sizeof(*fpci_mmio_regions);

	fpci_bus_info_t* root_bus = NULL;
	status = fpci_bus_lookup(0, true, &root_bus);
	if (status != ferr_ok) {
		fpanic("No root bus");
	}

	fpci_device_info_t* root_device = NULL;
	status = fpci_device_lookup(root_bus, 0, true, &root_device);
	if (status != ferr_ok) {
		fpanic("No root device");
	}

	// scan bus 0
	fpanic_status(fpci_bus_scan(root_bus));

	// if there are more host controllers, scan those as well
	//
	// note that we don't need the lock here since no other threads can
	// possible want to use PCI devices until we're done initializing ourselves
	simple_ghmap_for_each(&root_device->functions, fpci_root_bus_function_iterator, NULL);

	// DEBUGGING
	simple_ghmap_for_each(&fpci_buses, fpci_debug_bus_iterator, NULL);

	// start up pciman so userspace can register pci device drivers and query the pci tree
	fthread_t* pciman_thread = NULL;
	fpanic_status(fthread_new(pciman_thread_entry, NULL, NULL, 2ull * 1024 * 1024, 0, &pciman_thread));
	fpanic_status(fsched_manage(pciman_thread));
	fpanic_status(fthread_resume(pciman_thread));
};

FERRO_STRUCT(fpci_lookup_context) {
	uint16_t vendor_id;
	uint16_t device_id;
	fpci_device_t** out_device;
};

static bool fpci_lookup_function_iterator(void* context, simple_ghmap_t* hashmap, simple_ghmap_hash_t hash, const void* key, size_t key_size, void* entry, size_t entry_size) {
	fpci_lookup_context_t* ctx = context;
	fpci_function_info_t* info = entry;

	if (info->public.vendor_id == ctx->vendor_id && info->public.device_id == ctx->device_id) {
		if (ctx->out_device) {
			*ctx->out_device = &info->public;
		}
		return false;
	}

	return true;
};

static bool fpci_lookup_device_iterator(void* context, simple_ghmap_t* hashmap, simple_ghmap_hash_t hash, const void* key, size_t key_size, void* entry, size_t entry_size) {
	fpci_device_info_t* info = entry;
	return simple_ghmap_for_each(&info->functions, fpci_lookup_function_iterator, context) == ferr_ok;
};

static bool fpci_lookup_bus_iterator(void* context, simple_ghmap_t* hashmap, simple_ghmap_hash_t hash, const void* key, size_t key_size, void* entry, size_t entry_size) {
	fpci_bus_info_t* info = entry;
	return simple_ghmap_for_each(&info->devices, fpci_lookup_device_iterator, context) == ferr_ok;
};

ferr_t fpci_lookup(uint16_t vendor_id, uint16_t device_id, fpci_device_t** out_device) {
	fpci_lookup_context_t ctx = {
		.vendor_id = vendor_id,
		.device_id = device_id,
		.out_device = out_device,
	};
	ferr_t status;

	flock_spin_intsafe_lock(&fpci_tree_lock);
	status = simple_ghmap_for_each(&fpci_buses, fpci_lookup_bus_iterator, &ctx);
	flock_spin_intsafe_unlock(&fpci_tree_lock);

	if (status == ferr_cancelled) {
		return ferr_ok;
	} else if (status == ferr_ok) {
		return ferr_no_such_resource;
	} else {
		return status;
	}
};

ferr_t fpci_device_register_interrupt_handler(fpci_device_t* device, fpci_device_interrupt_handler_f handler, void* data) {
	fpci_function_info_t* function = (void*)device;
	return fpci_function_register_interrupt_handler(function, handler, data);
};

ferr_t fpci_device_get_mapped_bar(fpci_device_t* device, uint8_t bar_index, volatile uint32_t** out_bar, size_t* out_size) {
	ferr_t status = ferr_ok;
	fpci_function_info_t* info = (void*)device;
	fpci_bar_t* bar = &info->bars[bar_index];

	if (bar_index > 5) {
		status = ferr_invalid_argument;
		goto out_unlocked;
	}

	flock_spin_intsafe_lock(&info->lock);

	if (bar->type == fpci_bar_type_invalid) {
		status = ferr_no_such_resource;
		goto out;
	}

	if (bar->type != fpci_bar_type_memory) {
		status = ferr_invalid_argument;
		goto out;
	}

	// check if we've already mapped it
	if (bar->mapped_base > 0) {
		goto out;
	}

	// try to map it into memory
	status = fpage_map_kernel_any((void*)bar->physical_base, fpage_round_up_to_page_count(bar->size), (void*)&bar->mapped_base, fpage_flag_no_cache);
	if (status != ferr_ok) {
		goto out;
	}

	fconsole_logf("PCI: %02x:%02x.%x: mapped BAR%u from %p to %p (%zu bytes)\n", info->device->bus->location, info->device->location, info->location, bar_index, (void*)bar->physical_base, bar->mapped_base, bar->size);

out:
	if (status == ferr_ok) {
		if (out_bar) {
			*out_bar = bar->mapped_base;
		}
		if (out_size) {
			*out_size = bar->size;
		}
	}
	flock_spin_intsafe_unlock(&info->lock);
out_unlocked:
	return status;
};

ferr_t fpci_device_get_mapped_bar_mapping(fpci_device_t* device, uint8_t bar_index, fpage_mapping_t** out_mapping, size_t* out_size) {
	ferr_t status = ferr_ok;
	fpci_function_info_t* info = (void*)device;
	fpci_bar_t* bar = &info->bars[bar_index];
	fpage_mapping_t* mapping = NULL;

	if (bar_index > 5) {
		status = ferr_invalid_argument;
		goto out_unlocked;
	}

	flock_spin_intsafe_lock(&info->lock);

	if (bar->type == fpci_bar_type_invalid) {
		status = ferr_no_such_resource;
		goto out;
	}

	if (bar->type != fpci_bar_type_memory) {
		status = ferr_invalid_argument;
		goto out;
	}

	if (!bar->mapping) {
		status = fpage_mapping_new(fpage_round_up_to_page_count(bar->size), 0, &mapping);
		if (status != ferr_ok) {
			goto out;
		}

		status = fpage_mapping_bind(mapping, 0, fpage_round_up_to_page_count(bar->size), (void*)bar->physical_base, 0);
		if (status != ferr_ok) {
			goto out;
		}

		bar->mapping = mapping;
	}

	status = fpage_mapping_retain(bar->mapping);
	if (status != ferr_ok) {
		goto out;
	}

out:
	if (status == ferr_ok) {
		if (out_mapping) {
			*out_mapping = mapping;
		}
		if (out_size) {
			*out_size = bar->size;
		}
	} else if (mapping) {
		fpage_mapping_release(mapping);
	}
	flock_spin_intsafe_unlock(&info->lock);
out_unlocked:
	return status;
};

ferr_t fpci_device_get_mapped_bar_raw_index(fpci_device_t* device, uint8_t raw_bar_index, volatile uint32_t** out_bar, size_t* out_size) {
	ferr_t status = ferr_no_such_resource;
	fpci_function_info_t* info = (void*)device;

	if (raw_bar_index > 5) {
		status = ferr_invalid_argument;
		goto out;
	}

	for (uint8_t i = 0; i < sizeof(info->bars) / sizeof(*info->bars); ++i) {
		if (info->bars->raw_index != raw_bar_index) {
			continue;
		}

		if (info->bars->type == fpci_bar_type_invalid) {
			continue;
		}

		status = fpci_device_get_mapped_bar(device, i, out_bar, out_size);

		break;
	}

out:
	return status;
};

ferr_t fpci_device_enable_bus_mastering(fpci_device_t* device) {
	fpci_function_info_t* info = (void*)device;
	flock_spin_intsafe_lock(&info->lock);
	info->mmio_base[1] |= 1 << 2;
	flock_spin_intsafe_unlock(&info->lock);
	return ferr_ok;
};

ferr_t fpci_device_config_space_read(fpci_device_t* device, size_t offset, uint8_t size, void* out_data) {
	fpci_function_info_t* info = (void*)device;
	ferr_t status = ferr_ok;
	uint8_t* out_buf = out_data;

	flock_spin_intsafe_lock(&info->lock);

	if ((offset & 3) != 0) {
		// the offset is not a multiple of 4
		uint32_t val = info->mmio_base[offset / 4];

		val >>= (offset & 3) * 8;

		for (uint8_t i = 0; i < 4 - (offset & 3) && size > 0; (++i), (--size), (++out_buf), (val >>= 8)) {
			*out_buf = val & 0xff;
		}

		offset = (offset & ~3) + 4;
	}

	// the offset is a multiple of 4 here;
	// perform full copies as necessary
	for (; size >= 4; (offset += 4), (size -= 4), (out_buf += 4)) {
		uint32_t val = info->mmio_base[offset / 4];

		// TODO: check if this would need to be reversed for big-endian platforms
		out_buf[0] = (val >>  0) & 0xff;
		out_buf[1] = (val >>  8) & 0xff;
		out_buf[2] = (val >> 16) & 0xff;
		out_buf[3] = (val >> 24) & 0xff;
	}

	// the offset is a multiple of 4 here, but the size is less than 4
	if (size > 0) {
		uint32_t val = info->mmio_base[offset / 4];

		for (; size > 0; (--size), (++out_buf), (val >>= 8)) {
			*out_buf = val & 0xff;
		}
	}

	flock_spin_intsafe_unlock(&info->lock);

out:
	return status;
};

ferr_t fpci_device_config_space_write(fpci_device_t* device, size_t offset, uint8_t size, const void* data) {
	fpci_function_info_t* info = (void*)device;
	ferr_t status = ferr_ok;
	const uint8_t* buf = data;

	flock_spin_intsafe_lock(&info->lock);

	if ((offset & 3) != 0) {
		// the offset is not a multiple of 4
		uint32_t val = info->mmio_base[offset / 4];
		uint8_t local_offset = (offset & 3) * 8;

		val &= ~(0xffffffff << local_offset);

		for (uint8_t i = 0; i < 4 - (offset & 3) && size > 0; (++i), (--size), (++buf), (local_offset += 8)) {
			val |= (uint32_t)(*buf) << local_offset;
		}

		info->mmio_base[offset / 4] = val;

		offset = (offset & ~3) + 4;
	}

	// the offset is a multiple of 4 here;
	// perform full copies as necessary
	for (; size >= 4; (offset += 4), (size -= 4), (buf += 4)) {
		// TODO: check if this would need to be reversed for big-endian platforms
		info->mmio_base[offset / 4] =
			((uint32_t)buf[0] <<  0) |
			((uint32_t)buf[1] <<  8) |
			((uint32_t)buf[2] << 16) |
			((uint32_t)buf[3] << 24)
			;
	}

	// the offset is a multiple of 4 here, but the size is less than 4
	if (size > 0) {
		uint32_t val = info->mmio_base[offset / 4];
		uint8_t local_offset = 0;

		val &= ~(0xffffffff >> ((4 - size) * 8));

		for (; size > 0; (--size), (++buf), (local_offset += 8)) {
			val |= (uint32_t)(*buf) << local_offset;
		}

		info->mmio_base[offset / 4] = val;
	}

	flock_spin_intsafe_unlock(&info->lock);

out:
	return status;
};

FERRO_STRUCT(fpci_scan_context) {
	fpci_scan_iterator_f iterator;
	void* context;
	fpci_device_t** out_device;
};

static bool fpci_scan_function_iterator(void* context, simple_ghmap_t* hashmap, simple_ghmap_hash_t hash, const void* key, size_t key_size, void* entry, size_t entry_size) {
	fpci_scan_context_t* ctx = context;
	fpci_function_info_t* info = entry;

	if (ctx->iterator(ctx->context, &info->public)) {
		if (ctx->out_device) {
			*ctx->out_device = &info->public;
		}
		return false;
	}

	return true;
};

static bool fpci_scan_device_iterator(void* context, simple_ghmap_t* hashmap, simple_ghmap_hash_t hash, const void* key, size_t key_size, void* entry, size_t entry_size) {
	fpci_device_info_t* info = entry;
	return simple_ghmap_for_each(&info->functions, fpci_scan_function_iterator, context) == ferr_ok;
};

static bool fpci_scan_bus_iterator(void* context, simple_ghmap_t* hashmap, simple_ghmap_hash_t hash, const void* key, size_t key_size, void* entry, size_t entry_size) {
	fpci_bus_info_t* info = entry;
	return simple_ghmap_for_each(&info->devices, fpci_scan_device_iterator, context) == ferr_ok;
};

ferr_t fpci_scan(fpci_scan_iterator_f iterator, void* context, fpci_device_t** out_device) {
	fpci_scan_context_t ctx = {
		.iterator = iterator,
		.context = context,
		.out_device = out_device,
	};
	ferr_t status;

	flock_spin_intsafe_lock(&fpci_tree_lock);
	status = simple_ghmap_for_each(&fpci_buses, fpci_scan_bus_iterator, &ctx);
	flock_spin_intsafe_unlock(&fpci_tree_lock);

	if (status == ferr_cancelled) {
		return ferr_ok;
	} else if (status == ferr_ok) {
		return ferr_no_such_resource;
	} else {
		return status;
	}
};
