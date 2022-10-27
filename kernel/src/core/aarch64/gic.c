/*
 * This file is part of Anillo OS
 * Copyright (C) 2021 Anillo OS Developers
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
 * AARCH64 GIC (Generic Interrupt Controller) initialization and management.
 */

#include <ferro/core/aarch64/gic.h>
#include <ferro/core/interrupts.h>
#include <ferro/core/panic.h>
#include <ferro/core/acpi.h>
#include <ferro/core/console.h>
#include <ferro/core/paging.h>
#include <ferro/core/locks.h>
#include <libsimple/libsimple.h>

#define RESERVED_HELPER2(x, y) uint8_t reserved ## y [x]
#define RESERVED_HELPER(x, y) RESERVED_HELPER2(x, y)
#define RESERVED(x) RESERVED_HELPER(x, __COUNTER__)

#define VERIFY_OFFSET_GICR_LPI(member, offset) FERRO_VERIFY_OFFSET(     farch_gic_gicr_lpi_block_t, member, offset)
#define VERIFY_OFFSET_GICR_SGI(member, offset) FERRO_VERIFY_OFFSET(     farch_gic_gicr_sgi_block_t, member, offset)
#define VERIFY_OFFSET_GICD(member, offset)     FERRO_VERIFY_OFFSET(         farch_gic_gicd_block_t, member, offset)
#define VERIFY_OFFSET_CPU(member, offset)      FERRO_VERIFY_OFFSET(farch_gic_cpu_interface_block_t, member, offset)
#define VERIFY_OFFSET_ITS(member, offset)      FERRO_VERIFY_OFFSET(          farch_gic_its_block_t, member, offset)

FERRO_PACKED_STRUCT(farch_gic_gicr_lpi_block) {
	volatile uint32_t control;
	volatile uint32_t implementer_id;
	volatile uint64_t controller_type;
	volatile uint32_t status;
	volatile uint32_t wake;
	volatile uint32_t max_partid_pmg;
	volatile uint32_t partid_pmg;
	RESERVED(32);
	volatile uint64_t set_lpi;
	volatile uint64_t clear_lpi;
	RESERVED(32);
	volatile uint64_t properties_base;
	volatile uint64_t pending_base;
	RESERVED(32);
	volatile uint64_t invalidate_lpi;
	RESERVED(8);
	volatile uint64_t invalidate_all;
	RESERVED(8);
	volatile uint32_t synchronize;
	RESERVED(65292);
	volatile uint32_t identifiers[12];
};

VERIFY_OFFSET_GICR_LPI(control, 0x00);
VERIFY_OFFSET_GICR_LPI(implementer_id, 0x04);
VERIFY_OFFSET_GICR_LPI(controller_type, 0x08);
VERIFY_OFFSET_GICR_LPI(status, 0x10);
VERIFY_OFFSET_GICR_LPI(wake, 0x14);
VERIFY_OFFSET_GICR_LPI(max_partid_pmg, 0x18);
VERIFY_OFFSET_GICR_LPI(partid_pmg, 0x1c);
VERIFY_OFFSET_GICR_LPI(set_lpi, 0x40);
VERIFY_OFFSET_GICR_LPI(clear_lpi, 0x48);
VERIFY_OFFSET_GICR_LPI(properties_base, 0x70);
VERIFY_OFFSET_GICR_LPI(pending_base, 0x78);
VERIFY_OFFSET_GICR_LPI(invalidate_lpi, 0xa0);
VERIFY_OFFSET_GICR_LPI(invalidate_all, 0xb0);
VERIFY_OFFSET_GICR_LPI(synchronize, 0xc0);
VERIFY_OFFSET_GICR_LPI(identifiers, 0xffd0);

FERRO_PACKED_STRUCT(farch_gic_gicr_sgi_block) {
	RESERVED(128);
	volatile uint32_t groups[3];
	RESERVED(116);
	volatile uint32_t set_enabled_on_write[3];

	RESERVED(116);
	volatile uint32_t clear_enabled_on_write[3];
	RESERVED(116);
	volatile uint32_t set_pending_on_write[3];
	RESERVED(116);
	volatile uint32_t clear_pending_on_write[3];
	RESERVED(116);
	volatile uint32_t set_active_on_write[3];
	RESERVED(116);
	volatile uint32_t clear_active_on_write[3];
	RESERVED(116);
	volatile uint32_t priorities[24];
	RESERVED(1952);
	volatile uint32_t configurations[6];
	RESERVED(232);
	volatile uint32_t group_modifiers[3];
	RESERVED(244);
	volatile uint32_t non_secure_access_control;
	RESERVED(61948);
};

VERIFY_OFFSET_GICR_SGI(groups, 0x80);
VERIFY_OFFSET_GICR_SGI(set_enabled_on_write, 0x100);
VERIFY_OFFSET_GICR_SGI(clear_enabled_on_write, 0x180);
VERIFY_OFFSET_GICR_SGI(set_pending_on_write, 0x200);
VERIFY_OFFSET_GICR_SGI(clear_pending_on_write, 0x280);
VERIFY_OFFSET_GICR_SGI(set_active_on_write, 0x300);
VERIFY_OFFSET_GICR_SGI(clear_active_on_write, 0x380);
VERIFY_OFFSET_GICR_SGI(priorities, 0x400);
VERIFY_OFFSET_GICR_SGI(configurations, 0xc00);
VERIFY_OFFSET_GICR_SGI(group_modifiers, 0xd00);
VERIFY_OFFSET_GICR_SGI(non_secure_access_control, 0xe00);

FERRO_PACKED_STRUCT(farch_gic_gicr_block) {
	farch_gic_gicr_lpi_block_t lpi;
	farch_gic_gicr_sgi_block_t sgi;
};

FERRO_VERIFY_OFFSET(farch_gic_gicr_block_t, lpi, 0);
FERRO_VERIFY_OFFSET(farch_gic_gicr_block_t, sgi, 0x10000);

FERRO_PACKED_STRUCT(farch_gic_gicd_block) {
	volatile uint32_t control;
	volatile uint32_t controller_type;
	volatile uint32_t implementer_id;
	volatile uint32_t controller_type2;
	volatile uint32_t status;
	RESERVED(44);
	volatile uint32_t set_spi_on_write;
	RESERVED(4);
	volatile uint32_t clear_spi_on_write;
	RESERVED(4);
	volatile uint32_t set_spi_on_write_secure;
	RESERVED(4);
	volatile uint32_t clear_spi_on_write_secure;
	RESERVED(36);
	volatile uint32_t groups[32];
	volatile uint32_t set_enabled_on_write[32];
	volatile uint32_t clear_enabled_on_write[32];
	volatile uint32_t set_pending_on_write[32];
	volatile uint32_t clear_pending_on_write[32];
	volatile uint32_t set_active_on_write[32];
	volatile uint32_t clear_active_on_write[32];
	volatile uint32_t priorities[255];
	RESERVED(4);
	volatile uint32_t target_processors[255];
	RESERVED(4);
	volatile uint32_t configurations[64];
	volatile uint32_t group_modifiers[32];
	RESERVED(128);
	volatile uint32_t non_secure_access_controls[64];
	volatile uint32_t sgi;
	RESERVED(12);
	volatile uint32_t sgi_clear_pending_on_write[4];
	volatile uint32_t sgi_set_pending_on_write[4];
	RESERVED(208);
	volatile uint32_t extended_groups[32];
	RESERVED(384);
	volatile uint32_t extended_set_enabled_on_write[32];
	RESERVED(384);
	volatile uint32_t extended_clear_enabled_on_write[32];
	RESERVED(384);
	volatile uint32_t extended_set_pending_on_write[32];
	RESERVED(384);
	volatile uint32_t extended_clear_pending_on_write[32];
	RESERVED(384);
	volatile uint32_t extended_set_active_on_write[32];
	RESERVED(384);
	volatile uint32_t extended_clear_active_on_write[32];
	RESERVED(896);
	volatile uint32_t extended_priorities[256];
	RESERVED(3072);
	volatile uint32_t extended_configurations[64];
	RESERVED(768);
	volatile uint32_t extended_group_modifiers[32];
	RESERVED(384);
	volatile uint32_t extended_non_secure_access_controls[32];
	RESERVED(10880);
	volatile uint64_t routers[988];
	RESERVED(32);
	volatile uint64_t extended_routers[1024];
	RESERVED(24528);
	volatile uint32_t identifiers[12];
};

VERIFY_OFFSET_GICD(control, 0);
VERIFY_OFFSET_GICD(controller_type, 0x04);
VERIFY_OFFSET_GICD(implementer_id, 0x08);
VERIFY_OFFSET_GICD(controller_type2, 0x0c);
VERIFY_OFFSET_GICD(status, 0x10);
VERIFY_OFFSET_GICD(set_spi_on_write, 0x40);
VERIFY_OFFSET_GICD(clear_spi_on_write, 0x48);
VERIFY_OFFSET_GICD(set_spi_on_write_secure, 0x50);
VERIFY_OFFSET_GICD(clear_spi_on_write_secure, 0x58);
VERIFY_OFFSET_GICD(groups, 0x80);
VERIFY_OFFSET_GICD(set_enabled_on_write, 0x100);
VERIFY_OFFSET_GICD(clear_enabled_on_write, 0x180);
VERIFY_OFFSET_GICD(set_pending_on_write, 0x200);
VERIFY_OFFSET_GICD(clear_pending_on_write, 0x280);
VERIFY_OFFSET_GICD(set_active_on_write, 0x300);
VERIFY_OFFSET_GICD(clear_active_on_write, 0x380);
VERIFY_OFFSET_GICD(priorities, 0x400);
VERIFY_OFFSET_GICD(target_processors, 0x800);
VERIFY_OFFSET_GICD(configurations, 0xc00);
VERIFY_OFFSET_GICD(group_modifiers, 0xd00);
VERIFY_OFFSET_GICD(non_secure_access_controls, 0xe00);
VERIFY_OFFSET_GICD(sgi, 0xf00);
VERIFY_OFFSET_GICD(sgi_clear_pending_on_write, 0xf10);
VERIFY_OFFSET_GICD(sgi_set_pending_on_write, 0xf20);
VERIFY_OFFSET_GICD(extended_groups, 0x1000);
VERIFY_OFFSET_GICD(extended_set_enabled_on_write, 0x1200);
VERIFY_OFFSET_GICD(extended_clear_enabled_on_write, 0x1400);
VERIFY_OFFSET_GICD(extended_set_pending_on_write, 0x1600);
VERIFY_OFFSET_GICD(extended_clear_pending_on_write, 0x1800);
VERIFY_OFFSET_GICD(extended_set_active_on_write, 0x1a00);
VERIFY_OFFSET_GICD(extended_clear_active_on_write, 0x1c00);
VERIFY_OFFSET_GICD(extended_priorities, 0x2000);
VERIFY_OFFSET_GICD(extended_configurations, 0x3000);
VERIFY_OFFSET_GICD(extended_group_modifiers, 0x3400);
VERIFY_OFFSET_GICD(extended_non_secure_access_controls, 0x3600);
VERIFY_OFFSET_GICD(routers, 0x6100);
VERIFY_OFFSET_GICD(extended_routers, 0x8000);
VERIFY_OFFSET_GICD(identifiers, 0xffd0);

FERRO_PACKED_STRUCT(farch_gic_cpu_interface_block) {
	volatile uint32_t control;
	volatile uint32_t priority_mask;
	volatile uint32_t binary_point;
	volatile uint32_t interrupt_acknowledge;
	volatile uint32_t end_of_interrupt;
	volatile uint32_t running_priority;
	volatile uint32_t highest_priority_pending_interrupt;
	volatile uint32_t aliased_binary_point;
	volatile uint32_t aliased_interrupt_acknowledge;
	volatile uint32_t aliased_end_of_interrupt;
	volatile uint32_t aliased_highest_priority_pending_interrupt;
	volatile uint32_t status;
	RESERVED(160);
	volatile uint32_t active_priorities[4];
	volatile uint32_t non_secure_active_priorities[4];
	RESERVED(12);
	volatile uint32_t interface_id;
	RESERVED(3840);
	volatile uint32_t deactivate_interrupt;
};

VERIFY_OFFSET_CPU(control, 0);
VERIFY_OFFSET_CPU(priority_mask, 0x04);
VERIFY_OFFSET_CPU(binary_point, 0x08);
VERIFY_OFFSET_CPU(interrupt_acknowledge, 0x0c);
VERIFY_OFFSET_CPU(end_of_interrupt, 0x10);
VERIFY_OFFSET_CPU(running_priority, 0x14);
VERIFY_OFFSET_CPU(highest_priority_pending_interrupt, 0x18);
VERIFY_OFFSET_CPU(aliased_binary_point, 0x1c);
VERIFY_OFFSET_CPU(aliased_interrupt_acknowledge, 0x20);
VERIFY_OFFSET_CPU(aliased_end_of_interrupt, 0x24);
VERIFY_OFFSET_CPU(aliased_highest_priority_pending_interrupt, 0x28);
VERIFY_OFFSET_CPU(status, 0x2c);
VERIFY_OFFSET_CPU(active_priorities, 0xd0);
VERIFY_OFFSET_CPU(non_secure_active_priorities, 0xe0);
VERIFY_OFFSET_CPU(interface_id, 0xfc);
VERIFY_OFFSET_CPU(deactivate_interrupt, 0x1000);

FERRO_PACKED_STRUCT(farch_gic_its_block) {
	volatile uint32_t control;
	volatile uint32_t implementer_id;
	volatile uint32_t controller_type;
	RESERVED(4);
	volatile uint32_t max_partid_pmg;
	volatile uint32_t partid_pmg;
	volatile uint32_t mpid;
	RESERVED(36);
	volatile uint32_t status;
	RESERVED(4);
	volatile uint64_t unmapped_msi;
	RESERVED(48);
	volatile uint64_t command_queue_descriptor;
	volatile uint64_t write;
	volatile uint64_t read;
	RESERVED(104);
	volatile uint64_t translation_table_descriptors[8];
	RESERVED(65168);
	volatile uint32_t identifiers[12];
};

VERIFY_OFFSET_ITS(control, 0);
VERIFY_OFFSET_ITS(implementer_id, 0x04);
VERIFY_OFFSET_ITS(controller_type, 0x08);
VERIFY_OFFSET_ITS(max_partid_pmg, 0x10);
VERIFY_OFFSET_ITS(partid_pmg, 0x14);
VERIFY_OFFSET_ITS(mpid, 0x18);
VERIFY_OFFSET_ITS(status, 0x40);
VERIFY_OFFSET_ITS(unmapped_msi, 0x48);
VERIFY_OFFSET_ITS(command_queue_descriptor, 0x80);
VERIFY_OFFSET_ITS(write, 0x88);
VERIFY_OFFSET_ITS(read, 0x90);
VERIFY_OFFSET_ITS(translation_table_descriptors, 0x100);
VERIFY_OFFSET_ITS(identifiers, 0xffd0);

// this block probably has more registers, but we don't need them
// and i couldn't find any documentation on them anywhere
FERRO_PACKED_STRUCT(farch_gic_msi_block) {
	RESERVED(8);
	volatile uint32_t type;
	RESERVED(52);
	volatile uint32_t set_spi;
	RESERVED(3976);
	volatile uint32_t iidr;
};

FERRO_STRUCT(farch_gic_interrupt_handler_entry) {
	farch_gic_interrupt_handler_f handler;
	void* context;
	flock_spin_intsafe_t lock;
	bool for_group_0;
};

#define FARCH_GIC_SPI_MIN 32
#define FARCH_GIC_INTERRUPT_COUNT 1020

FERRO_STRUCT(farch_gic_msi_frame) {
	volatile farch_gic_msi_block_t* block;
	volatile farch_gic_msi_block_t* physical_block;
	uint32_t spi_base;
	uint32_t spi_count;
};

static bool needs_separate_deactivate = false;
static bool use_system_registers = false;

static farch_gic_gicd_block_t* gicd = NULL;
static farch_gic_gicr_block_t* gicr = NULL;
static farch_gic_cpu_interface_block_t* cpu_interface = NULL;
static uint8_t gic_version = 0;
static farch_gic_interrupt_handler_entry_t handlers[FARCH_GIC_INTERRUPT_COUNT] = {0};
static farch_gic_msi_frame_t msi_frames[64] = {0};
static size_t msi_frame_count = 0;
static flock_spin_intsafe_t allocated_spi_bitmap_lock = FLOCK_SPIN_INTSAFE_INIT;
static uint8_t allocated_spi_bitmap[(FARCH_GIC_INTERRUPT_COUNT - FARCH_GIC_SPI_MIN + 7) / 8] = {0};

static void signal_eoi(uint64_t interrupt_number, bool is_group_0) {
	if (use_system_registers) {
		if (is_group_0) {
			__asm__ volatile("msr icc_eoir0_el1, %0" :: "r" (interrupt_number));
		} else {
			__asm__ volatile("msr icc_eoir1_el1, %0" :: "r" (interrupt_number));
		}

		if (needs_separate_deactivate) {
			__asm__ volatile("msr icc_dir_el1, %0" :: "r" (interrupt_number));
		}
	} else {
		if (is_group_0) {
			cpu_interface->end_of_interrupt = (uint32_t)interrupt_number;
		} else {
			cpu_interface->aliased_end_of_interrupt = (uint32_t)interrupt_number;
		}

		if (needs_separate_deactivate) {
			cpu_interface->deactivate_interrupt = (uint32_t)interrupt_number;
		}
	}
};

static uint64_t read_interrupt_number(bool is_group_0) {
	uint64_t result;

	if (use_system_registers) {
		if (is_group_0) {
			__asm__ volatile("mrs %0, icc_iar0_el1" : "=r" (result));
		} else {
			__asm__ volatile("mrs %0, icc_iar1_el1" : "=r" (result));
		}
	} else {
		if (is_group_0) {
			result = cpu_interface->interrupt_acknowledge;
		} else {
			result = cpu_interface->aliased_interrupt_acknowledge;
		}
	}

	return result;
};

static void set_interrupts_enabled(bool enabled, bool for_group_0) {
	if (use_system_registers) {
		uint64_t value = enabled ? 1 : 0;
		if (for_group_0) {
			__asm__ volatile("msr icc_igrpen0_el1, %0" :: "r" (value));
		} else {
			__asm__ volatile("msr icc_igrpen1_el1, %0" :: "r" (value));
		}
	} else {
		if (for_group_0) {
			cpu_interface->control |= 1 << 0;
		} else {
			cpu_interface->control |= 1 << 1;
		}
	}
};

static bool get_interrupts_enabled(bool for_group_0) {
	if (use_system_registers) {
		uint64_t value;
		if (for_group_0) {
			__asm__ volatile("mrs %0, icc_igrpen0_el1" : "=r" (value));
		} else {
			__asm__ volatile("mrs %0, icc_igrpen1_el1" : "=r" (value));
		}
		return (value & 1) != 0;
	} else {
		if (for_group_0) {
			return (cpu_interface->control & (1 << 0)) != 0;
		} else {
			return (cpu_interface->control & (1 << 1)) != 0;
		}
	}
};

ferr_t farch_gic_interrupt_enabled_read(uint64_t interrupt, bool* out_enabled) {
	size_t index = interrupt / 32;
	uint32_t bit = 1 << (interrupt % 32);

	if (interrupt > 1019) {
		return ferr_invalid_argument;
	}

	if (out_enabled) {
		*out_enabled = (gicd->set_enabled_on_write[index] & bit) != 0;
	}

	return ferr_ok;
};

ferr_t farch_gic_interrupt_enabled_write(uint64_t interrupt, bool enabled) {
	size_t index = interrupt / 32;
	uint32_t bit = 1 << (interrupt % 32);

	if (interrupt > 1019) {
		return ferr_invalid_argument;
	}

	if (enabled) {
		gicd->set_enabled_on_write[index] |= bit;
	} else {
		gicd->clear_enabled_on_write[index] |= bit;
	}

	return ferr_ok;
};

ferr_t farch_gic_interrupt_pending_read(uint64_t interrupt, bool* out_pending) {
	size_t index = interrupt / 32;
	uint32_t bit = 1 << (interrupt % 32);

	if (interrupt > 1019) {
		return ferr_invalid_argument;
	}

	if (out_pending) {
		*out_pending = (gicd->set_pending_on_write[index] & bit) != 0;
	}

	return ferr_ok;
};

ferr_t farch_gic_interrupt_pending_write(uint64_t interrupt, bool pending) {
	size_t index = interrupt / 32;
	uint32_t bit = 1 << (interrupt % 32);

	if (interrupt > 1019) {
		return ferr_invalid_argument;
	}

	if (pending) {
		gicd->set_pending_on_write[index] |= bit;
	} else {
		gicd->clear_pending_on_write[index] |= bit;
	}

	return ferr_ok;
};

ferr_t farch_gic_interrupt_priority_write(uint64_t interrupt, uint8_t priority) {
	size_t index = interrupt / 4;
	size_t shift = (interrupt % 4) * 8;
	uint32_t mask = 0xff << shift;
	uint32_t value = ((uint32_t)priority) << shift;

	if (interrupt > 1019) {
		return ferr_invalid_argument;
	}

	gicd->priorities[index] = (gicd->priorities[index] & ~mask) | value;

	return ferr_ok;
};

ferr_t farch_gic_interrupt_target_core_write(uint64_t interrupt, uint8_t core) {
	size_t index = interrupt / 4;
	size_t shift = (interrupt % 4) * 8;
	uint32_t mask = 0xff << shift;
	uint32_t value = (1ULL << core) << shift;

	if (interrupt > 1019) {
		return ferr_invalid_argument;
	}

	gicd->target_processors[index] = (gicd->target_processors[index] & ~mask) | value;

	return ferr_ok;
};

ferr_t farch_gic_interrupt_configuration_write(uint64_t interrupt, farch_gic_interrupt_configuration_t configuration) {
	size_t index = interrupt / 16;
	size_t shift = (interrupt % 16) * 2;
	uint32_t mask = 3 << shift;
	uint32_t value = configuration << shift;

	if (interrupt > 1019) {
		return ferr_invalid_argument;
	}

	gicd->configurations[index] = (gicd->configurations[index] & ~mask) | value;

	return ferr_ok;
};

static void irq_handler(bool is_fiq, fint_frame_t* frame) {
	uint64_t interrupt_number;
	// we only use group 0 interrupts for now
	bool is_group_0 = true;

	while (true) {
		farch_gic_interrupt_handler_entry_t* entry;
		farch_gic_interrupt_handler_f handler = NULL;
		void* context = NULL;
		interrupt_number = read_interrupt_number(is_group_0);

		if (interrupt_number >= 1020 && interrupt_number <= 1023) {
			break;
		}

		if (interrupt_number > 1019) {
			fpanic("Interrupt numbers greater than 1019 are currently unsupported");
		}

		entry = &handlers[interrupt_number];

		flock_spin_intsafe_lock(&entry->lock);
		if (entry->handler && entry->for_group_0 == is_group_0) {
			handler = entry->handler;
			context = entry->context;
		}
		flock_spin_intsafe_unlock(&entry->lock);

		if (!handler) {
			fpanic("No handler for interrupt %llu on group %s", interrupt_number, is_group_0 ? "0" : "1");
		}

		handler(context, frame);

		signal_eoi(interrupt_number, is_group_0);
	}
};

static bool system_register_access_is_supported(void) {
	uint64_t value;
	__asm__ volatile("mrs %0, id_aa64pfr0_el1" : "=r" (value));
	return (value & (1 << 24)) != 0;
};

static bool system_register_access_is_enabled(void) {
	uint64_t value;
	__asm__ volatile("mrs %0, icc_sre_el1" : "=r" (value));
	return (value & (1 << 0)) != 0;
};

static void set_system_register_access_is_enabled(bool enabled) {
	uint64_t value;
	__asm__ volatile("mrs %0, icc_sre_el1" : "=r" (value));
	if (enabled) {
		value |= 1ULL << 0;
	} else {
		value &= ~(1ULL << 0);
	}
	__asm__ volatile("msr icc_sre_el1, %0" :: "r" (value));
};

ferr_t farch_gic_register_handler(uint64_t interrupt, bool for_group_0, farch_gic_interrupt_handler_f handler, void* context) {
	ferr_t status = ferr_ok;
	farch_gic_interrupt_handler_entry_t* entry;

	if (interrupt > 1019 || !handler) {
		status = ferr_invalid_argument;
		goto out_unlocked;
	}

	entry = &handlers[interrupt];

	flock_spin_intsafe_lock(&entry->lock);

	if (entry->handler) {
		status = ferr_temporary_outage;
		goto out;
	}

	entry->handler = handler;
	entry->context = context;
	entry->for_group_0 = for_group_0;

out:
	flock_spin_intsafe_unlock(&entry->lock);
out_unlocked:
	return status;
};

ferr_t farch_gic_unregister_handler(uint64_t interrupt, bool for_group_0) {
	ferr_t status = ferr_ok;
	farch_gic_interrupt_handler_entry_t* entry;

	if (interrupt > 1019) {
		status = ferr_invalid_argument;
		goto out_unlocked;
	}

	entry = &handlers[interrupt];

	flock_spin_intsafe_lock(&entry->lock);

	if (!entry->handler || entry->for_group_0 != for_group_0) {
		status = ferr_no_such_resource;
		goto out;
	}

	entry->handler = NULL;
	entry->context = NULL;

out:
	flock_spin_intsafe_unlock(&entry->lock);
out_unlocked:
	return status;
};

void farch_gic_init(void) {
	facpi_madt_t* madt = NULL;

	if (system_register_access_is_supported()) {
		if (system_register_access_is_enabled()) {
			set_system_register_access_is_enabled(false);
			if (system_register_access_is_enabled()) {
				fpanic("GIC system register access is mandatory on this machine (but this is currently unsupported)");
			}
		}
	}

	madt = (facpi_madt_t*)facpi_find_table("APIC");
	if (!madt) {
		fpanic("No APIC table");
	}

	for (size_t offset = 0; offset < madt->header.length - offsetof(facpi_madt_t, entries); /* handled in the body */) {
		facpi_madt_entry_header_t* header = (void*)&madt->entries[offset];

		if (header->type == facpi_madt_entry_type_gicc) {
			facpi_madt_entry_gicc_t* entry = (void*)header;

			if (entry->base) {
				if (fpage_map_kernel_any((void*)entry->base, fpage_round_up_to_page_count(sizeof(farch_gic_cpu_interface_block_t)), (void**)&cpu_interface, 0) != ferr_ok) {
					fconsole_log("warning: Failed to map GIC CPU interface registers block\n");
				}
			}

			if (entry->gicr_base) {
				if (fpage_map_kernel_any((void*)entry->gicr_base, fpage_round_up_to_page_count(sizeof(farch_gic_gicr_block_t)), (void**)&gicr, 0) != ferr_ok) {
					fconsole_log("warning: Failed to map GIC redistributor registers block\n");
				}
			}
		} else if (header->type == facpi_madt_entry_type_gicd) {
			facpi_madt_entry_gicd_t* entry = (void*)header;

			if (entry->base) {
				if (fpage_map_kernel_any((void*)entry->base, fpage_round_up_to_page_count(sizeof(farch_gic_gicd_block_t)), (void**)&gicd, 0) != ferr_ok) {
					fconsole_log("warning: Failed to map GIC distributor registers block\n");
				}
			}

			gic_version = entry->gic_version;

			fconsole_logf("info: Found a GICv%u controller\n", gic_version);
		} else if (header->type == facpi_madt_entry_type_gic_msi) {
			facpi_madt_entry_gic_msi_t* entry = (void*)header;

			fconsole_logf("info: Found GICv2m MSI frame @ %p\n", (void*)entry->base);

			if (msi_frame_count >= sizeof(msi_frames) / sizeof(*msi_frames)) {
				fconsole_log("warning: Reached maximum number of GICv2m MSI frames; ignoring new frame\n");
			} else if (fpage_map_kernel_any((void*)entry->base, fpage_round_up_to_page_count(sizeof(farch_gic_msi_block_t)), (void*)&msi_frames[msi_frame_count].block, fpage_flag_no_cache) != ferr_ok) {
				fconsole_log("warning: Failed to map MSI frame\n");
			} else {
				farch_gic_msi_frame_t* msi_frame = &msi_frames[msi_frame_count];

				++msi_frame_count;

				msi_frame->physical_block = (void*)entry->base;
				msi_frame->spi_base = (entry->flags & facpi_madt_entry_gic_msi_flag_spi_select) != 0 ? entry->spi_base : ((msi_frame->block->type >> 16) & 0x3ff);
				msi_frame->spi_count = (entry->flags & facpi_madt_entry_gic_msi_flag_spi_select) != 0 ? entry->spi_count : ((msi_frame->block->type >> 0) & 0x3ff);

				fconsole_logf("GICv2m MSI frame base=%u, count=%u\n", msi_frame->spi_base, msi_frame->spi_count);
			}
		}

		offset += header->length;
	}

	// with GICv3, we might not have a CPU interface registers block,
	// but we'll always have the GIC distributor registers block
	if (!gicd) {
		fpanic("No GIC distributor registers block found");
	}

	// with GICv2, we MUST have a CPU interface registers block
	if (!use_system_registers && !cpu_interface) {
		fpanic("Must use mmio CPU interface, but no block for it was found");
	}

	gicd->control &= ~((1 << 0) | (1 << 1));

	if (gic_version > 2) {
		if ((gicd->control & (1 << 6)) == 0) {
			fconsole_log("info: GIC security is enabled; disabling it...\n");

			gicd->control |= 1 << 6;

			if ((gicd->control & (1 << 6)) == 0) {
				fpanic("Failed to disable GIC security");
			}
		}
	}

	gicd->control |= (1 << 0);
	cpu_interface->control |= (1 << 0);

	cpu_interface->priority_mask = 0xff;
	cpu_interface->binary_point = 0;

	if (use_system_registers) {
		uint64_t icc_control;
		__asm__ volatile("mrs %0, icc_ctlr_el1" : "=r" (icc_control));
		needs_separate_deactivate = (icc_control & (1 << 1)) != 0;
	} else {
		needs_separate_deactivate = (cpu_interface->control & (1 << 9)) != 0;
	}

	for (size_t i = 0; i < sizeof(handlers) / sizeof(*handlers); ++i) {
		flock_spin_intsafe_init(&handlers[i].lock);
	}

	// assign all interrupts to group 0
	for (size_t i = 0; i < sizeof(gicd->groups) / sizeof(*gicd->groups); ++i) {
		gicd->groups[i] = 0;
	}

	farch_int_set_irq_handler(irq_handler);
};

ferr_t farch_gic_allocate_msi_interrupt(uint64_t* out_interrupt, uint32_t* out_msi_data, uint64_t* out_msi_address) {
	ferr_t status = ferr_ok;
	uint64_t interrupt = UINT64_MAX;
	farch_gic_msi_frame_t* msi_frame = NULL;
	uint32_t msi_data = 0;
	uint64_t msi_address = 0;

	flock_spin_intsafe_lock(&allocated_spi_bitmap_lock);

	for (uint64_t i = 0; i < FARCH_GIC_INTERRUPT_COUNT - FARCH_GIC_SPI_MIN; ++i) {
		uint64_t interrupt_number = i + FARCH_GIC_SPI_MIN;

		if ((allocated_spi_bitmap[i / 8] & (1 << (i % 8))) != 0) {
			continue;
		}

		// find the MSI frame for this interrupt
		for (size_t j = 0; j < msi_frame_count; ++j) {
			farch_gic_msi_frame_t* this_frame = &msi_frames[j];

			if (this_frame->spi_base <= interrupt_number && this_frame->spi_base + this_frame->spi_count > interrupt_number) {
				msi_frame = this_frame;
				break;
			}
		}

		// we always mark the interrupt as in-use once we get here because either:
		//   * we found the MSI frame and we're going to use it now, or
		//   * this interrupt doesn't actually exist, so we mark it as in-use so we don't waste our time checking it again later
		allocated_spi_bitmap[i / 8] |= (1 << (i % 8));

		if (!msi_frame) {
			continue;
		}

		interrupt = interrupt_number;
		break;
	}

	flock_spin_intsafe_unlock(&allocated_spi_bitmap_lock);

	if (interrupt == UINT64_MAX) {
		status = ferr_resource_unavailable;
		goto out;
	}

	msi_address = (uintptr_t)&msi_frame->physical_block->set_spi;
	msi_data = interrupt;

	*out_interrupt = interrupt;
	*out_msi_data = msi_data;
	*out_msi_address = msi_address;

out:
	return status;
};
