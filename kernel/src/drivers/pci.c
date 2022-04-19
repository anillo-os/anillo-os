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

#include <libsimple/libsimple.h>

#define MAX_DEVICES_PER_BUS 32
#define MAX_FUNCTIONS_PER_DEVICE 8

static flock_spin_intsafe_t fpci_buses_lock = FLOCK_SPIN_INTSAFE_INIT;
static simple_ghmap_t fpci_buses;

static const fpci_mcfg_entry_t* fpci_mmio_regions = NULL;
static size_t fpci_mmio_region_count = 0;

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

	flock_spin_intsafe_lock(&fpci_buses_lock);

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

		flock_spin_intsafe_init(&info->devices_lock);

		status = simple_ghmap_init(&info->devices, 0, 0, simple_ghmap_allocate_mempool, simple_ghmap_free_mempool, NULL, NULL, NULL, NULL, NULL, NULL);
		if (status != ferr_ok) {
			FERRO_WUR_IGNORE(simple_ghmap_clear_h(&fpci_buses, bus));
			goto out;
		}
	}

out:
	flock_spin_intsafe_unlock(&fpci_buses_lock);
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

	flock_spin_intsafe_lock(&bus->devices_lock);

	status = simple_ghmap_lookup_h(&bus->devices, device, create_if_absent, sizeof(fpci_device_info_t), &created, (void*)&info, NULL);
	if (status != ferr_ok) {
		goto out;
	}

	if (created) {
		info->bus = bus;
		info->location = device;

		flock_spin_intsafe_init(&info->functions_lock);

		// initial size is 1 because we're guaranteed to have at least 1 function
		status = simple_ghmap_init(&info->functions, 1, 0, simple_ghmap_allocate_mempool, simple_ghmap_free_mempool, NULL, NULL, NULL, NULL, NULL, NULL);
		if (status != ferr_ok) {
			FERRO_WUR_IGNORE(simple_ghmap_clear_h(&bus->devices, device));
			goto out;
		}

		status = fpci_function_lookup(info, 0, true, &info->function0);
		if (status != ferr_ok) {
			// function doesn't exist (and therefore neither does the device)
			FERRO_WUR_IGNORE(simple_ghmap_clear_h(&bus->devices, device));
			goto out;
		}

		// at this point, we definitely have a valid device
	}

out:
	flock_spin_intsafe_unlock(&bus->devices_lock);
out_unlocked:
	if (status == ferr_ok) {
		if (out_device) {
			*out_device = info;
		}
	}
	return status;
};

ferr_t fpci_function_lookup(fpci_device_info_t* device, uint8_t function, bool create_if_absent, fpci_function_info_t** out_function) {
	ferr_t status = ferr_ok;
	bool created = false;
	fpci_function_info_t* info = NULL;

	flock_spin_intsafe_lock(&device->functions_lock);

	status = simple_ghmap_lookup_h(&device->functions, function, create_if_absent, sizeof(fpci_function_info_t), &created, (void*)&info, NULL);
	if (status != ferr_ok) {
		goto out;
	}

	if (created) {
		info->device = device;
		info->location = function;

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
		info->vendor_id = tmp & 0xffffu;
		info->device_id = tmp >> 16;

		tmp = info->mmio_base[2];
		info->class_code = tmp >> 24;
		info->subclass_code = (tmp >> 16) & 0xff;
		info->programming_interface = (tmp >> 8) & 0xff;
	}

out:
	flock_spin_intsafe_unlock(&device->functions_lock);
out_unlocked:
	if (status == ferr_ok) {
		if (out_function) {
			*out_function = info;
		}
	}
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

	if (
		function->class_code == 0x06 && function->subclass_code == 0x04 &&

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
	fconsole_logf("Found %02x:%02x.%x (VID = 0x%04x, DID = 0x%04x, class code = 0x%02x, subclass code = 0x%02x, programming interface = 0x%02x)\n", function->device->bus->location, function->device->location, function->location, function->vendor_id, function->device_id, function->class_code, function->subclass_code, function->programming_interface);
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
};
