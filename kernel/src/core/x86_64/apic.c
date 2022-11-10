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
 * x86_64 APIC management, including timer backends.
 *
 * This file also handles a couple of the `fcpu` functions.
 */

#include <ferro/core/x86_64/apic.h>
#include <ferro/core/acpi.h>
#include <ferro/core/paging.h>
#include <ferro/core/panic.h>
#include <ferro/core/console.h>
#include <ferro/core/x86_64/legacy-io.h>
#include <ferro/core/interrupts.h>
#include <ferro/core/timers.private.h>
#include <ferro/core/x86_64/tsc.h>
#include <ferro/core/x86_64/msr.h>
#include <ferro/core/mempool.h>
#include <cpuid.h>
#include <ferro/core/cpu.private.h>
#include <libsimple/general.h>
#include <ferro/core/x86_64/smp-init.h>
#include <ferro/core/paging.private.h>

#define HZ_PER_KHZ 1000
#define MAX_CALIBRATION_ATTEMPTS 10
// XXX: this is kind of arbitrary
#define LAPIC_CYCLES 500000
// TODO: determine this
#define TSC_LOOP_MIN_COUNT 1
// XXX: this is also kind of arbitrary
#define TSC_MIN_DELTA_COEFFICIENT 1000

#define RESERVED_HELPER2(x, y) uint8_t reserved ## y [x]
#define RESERVED_HELPER(x, y) RESERVED_HELPER2(x, y)
#define RESERVED(x) RESERVED_HELPER(x, __COUNTER__)

#define REGISTER(name) volatile uint32_t name; RESERVED(12)
#define REGISTER64(name) \
	REGISTER(name ## _0_31); \
	REGISTER(name ## _32_63);
#define REGISTER256(name) \
	REGISTER(name ## _0_31); \
	REGISTER(name ## _32_63); \
	REGISTER(name ## _64_95); \
	REGISTER(name ## _96_127); \
	REGISTER(name ## _128_159); \
	REGISTER(name ## _160_191); \
	REGISTER(name ## _192_223); \
	REGISTER(name ## _224_255);

FERRO_PACKED_STRUCT(fapic_block) {
	RESERVED(32);
	REGISTER(lapic_id);
	REGISTER(lapic_version);
	RESERVED(64);
	REGISTER(task_priority);
	REGISTER(arbitration_priority);
	REGISTER(processor_priority);
	REGISTER(end_of_interrupt);
	REGISTER(remote_read);
	REGISTER(destination_format);
	REGISTER(logical_destination);
	REGISTER(spurious_interrupt_vector);
	REGISTER256(in_service);
	REGISTER256(trigger_mode);
	REGISTER256(interrupt_request);
	REGISTER(error_status);
	RESERVED(96);
	REGISTER(lvt_cmci);
	REGISTER64(interrupt_command);
	REGISTER(lvt_timer);
	REGISTER(lvt_thermal_sensor);
	REGISTER(lvt_performance_monitoring_counters);
	REGISTER(lvt_lint0);
	REGISTER(lvt_lint1);
	REGISTER(lvt_error);
	REGISTER(timer_initial_counter);
	REGISTER(timer_current_counter);
	RESERVED(64);
	REGISTER(timer_divide_configuration);
	RESERVED(16);
};

FERRO_ENUM(uint8_t, fpic_command) {
	fpic_command_initialize = 0x11,
};

FERRO_ENUM(uint8_t, fpic_mode) {
	fpic_mode_8086 = 0x01,
};

FERRO_ENUM(uint8_t, fapic_timer_mode) {
	fapic_timer_mode_oneshot      = 0,
	fapic_timer_mode_periodic     = 1,
	fapic_timer_mode_tsc_deadline = 2,
};

#define fapic_timer_mode_mask (3ULL << 17)

FERRO_ENUM(uint8_t, fapic_lvt_delivery_mode) {
	fapic_lvt_delivery_mode_fixed           = 0,
	fapic_lvt_delivery_mode_lowest_priority = 1,
	fapic_lvt_delivery_mode_smi             = 2,
	fapic_lvt_delivery_mode_nmi             = 4,
	fapic_lvt_delivery_mode_init            = 5,
	fapic_lvt_delivery_mode_start_up        = 6,
	fapic_lvt_delivery_mode_extint          = 7,
};

FERRO_OPTIONS(uint32_t, fapic_lvt_flags) {
	fapic_lvt_flag_masked           = 1 << 16,
	fapic_lvt_flag_edge_triggered   = 1 << 15,
	fapic_lvt_flag_level_triggered  = 0 << 15,
	fapic_lvt_flag_remote_irr       = 1 << 14,
	fapic_lvt_flag_active_high      = 0 << 13,
	fapic_lvt_flag_active_low       = 1 << 13,
	fapic_lvt_flag_delivery_pending = 1 << 12,
};

FERRO_ENUM(uint8_t, fapic_icr_destination_shorthand) {
	fapic_icr_destination_shorthand_none = 0,
	fapic_icr_destination_shorthand_self = 1,
	fapic_icr_destination_shorthand_all = 2,
	fapic_icr_destination_shorthand_all_except_self = 3,
};

FERRO_OPTIONS(uint32_t, fapic_icr_flags) {
	fapic_icr_flag_trigger_mode_edge = 0 << 15,
	fapic_icr_flag_trigger_mode_level = 1 << 15,
	fapic_icr_flag_level_deassert = 0 << 14,
	fapic_icr_flag_level_assert = 1 << 14,
	fapic_icr_flag_delivery_status_idle = 0 << 12,
	fapic_icr_flag_delivery_status_pending = 1 << 12,
	fapic_icr_flag_destination_mode_physical = 0 << 11,
	fapic_icr_flag_destination_mode_logical = 1 << 11,
};

static fapic_block_t* lapic = NULL;

static void ignore_interrupt(void* data, fint_frame_t* frame) {};

static void remap_and_disable_pic(void) {
	for (size_t i = 0x20; i < 0x30; ++i) {
		if (farch_int_register_handler(i, ignore_interrupt, NULL, 0) != ferr_ok) {
			fpanic("failed to register PIC interrupt handler for %zu", i);
		}
	}

#if 0
	// start PIC initialization
	farch_lio_write_u8(farch_lio_port_pic_primary_command, fpic_command_initialize);
	farch_lio_write_u8(farch_lio_port_pic_secondary_command, fpic_command_initialize);

	// tell each PIC its interrupt vector offset
	farch_lio_write_u8(farch_lio_port_pic_primary_data, 0x20);
	farch_lio_write_u8(farch_lio_port_pic_secondary_data, 0x28);

	// tell the primary PIC it has a secondary PIC on IRQ 2
	farch_lio_write_u8(farch_lio_port_pic_primary_data, 4);

	// tell the secondary PIC its IRQ number (2)
	farch_lio_write_u8(farch_lio_port_pic_secondary_data, 2);

	// initialize both to 8086 mode
	farch_lio_write_u8(farch_lio_port_pic_primary_data, fpic_mode_8086);
	farch_lio_write_u8(farch_lio_port_pic_secondary_data, fpic_mode_8086);
#endif

	// mask all interrupts on both
	farch_lio_write_u8(farch_lio_port_pic_primary_data, 0xff);
	farch_lio_write_u8(farch_lio_port_pic_secondary_data, 0xff);
};

static bool supports_tsc_deadline(void) {
	unsigned int eax;
	unsigned int ebx;
	unsigned int ecx;
	unsigned int edx;

	if (__get_cpuid(1, &eax, &ebx, &ecx, &edx)) {
		return (ecx & (1 << 24)) != 0;
	} else {
		return false;
	}
};

static bool supports_apic(void) {
	unsigned int eax;
	unsigned int ebx;
	unsigned int ecx;
	unsigned int edx;

	if (__get_cpuid(1, &eax, &ebx, &ecx, &edx)) {
		return (edx & (1 << 9)) != 0;
	} else {
		return false;
	}
};

static void arm_timer(uint64_t tsc_offset) {
	uint64_t tsc = farch_tsc_read_weak() + tsc_offset;
	farch_msr_write(farch_msr_tsc_deadline, tsc);
};

static void disarm_timer(void) {
	farch_msr_write(farch_msr_tsc_deadline, 0);
};

// this is the same for both the TSC-deadline and LAPIC timer backends
static void timer_interrupt_handler(void* data, fint_frame_t* frame) {
	// signal EOI here instead of after because it may never return here
	farch_apic_signal_eoi();
	ftimers_backend_fire();
};

static void tsc_deadline_schedule(uint64_t delay) {
	arm_timer(farch_tsc_ns_to_offset(delay));
};

static uint64_t tsc_deadline_current_timestamp(void) {
	return farch_tsc_read_weak();
};

static uint64_t tsc_deadline_delta_to_ns(ftimers_backend_timestamp_t initial, ftimers_backend_timestamp_t final) {
	return farch_tsc_offset_to_ns(final - initial);
};

static void tsc_deadline_cancel(void) {
	disarm_timer();
};

static ftimers_backend_t tsc_deadline_backend = {
	.name = "tsc",
	// this will be updated later
	.precision = 0,
	.schedule = tsc_deadline_schedule,
	.current_timestamp = tsc_deadline_current_timestamp,
	.delta_to_ns = tsc_deadline_delta_to_ns,
	.cancel = tsc_deadline_cancel,
};

static void timer_callback(void* data) {
	fconsole_logf("test timer fired with data: %p\n", data);
};

static fapic_timer_mode_t get_timer_mode(void) {
	return (lapic->lvt_timer & fapic_timer_mode_mask) >> 17;
};

static void set_timer_mode(fapic_timer_mode_t mode) {
	lapic->lvt_timer = (lapic->lvt_timer & ~fapic_timer_mode_mask) | ((uint32_t)mode << 17);
};

static bool is_timer_masked(void) {
	return (lapic->lvt_timer & (1ULL << 16)) != 0;
};

static void set_is_timer_masked(bool is_masked) {
	if (is_masked) {
		lapic->lvt_timer |= 1ULL << 16;
	} else {
		lapic->lvt_timer &= ~(1ULL << 16);
	}
};

// uses the TSC and polling to determine the LAPIC timer frequency, similar to the approach with the PIT for determing the TSC frequency in `tsc.c`
static uint64_t determine_lapic_frequency(void) {
	uint64_t initial_tsc = 0;
	uint64_t loop_initial_tsc = 0;
	uint64_t final_tsc = 0;
	uint64_t loop_count = 0;
	uint64_t delta_min = UINT64_MAX;
	uint64_t delta_max = 0;
	uint64_t delta = 0;
	fapic_timer_mode_t saved_mode = get_timer_mode();
	bool saved_is_masked = is_timer_masked();
	__uint128_t tmp;

	// setup the timer conditions
	// disable interrupts by masking it
	set_is_timer_masked(true);
	set_timer_mode(fapic_timer_mode_oneshot);

	// divide by 1
	lapic->timer_divide_configuration = 0x0b;

	// start the counter
	lapic->timer_initial_counter = LAPIC_CYCLES;

	//fconsole_logf("initial counter value: %u; current counter value: %u\n", lapic->timer_initial_counter, lapic->timer_current_counter);

	// read the initial TSC value
	initial_tsc = loop_initial_tsc = final_tsc = farch_tsc_read_weak();

	// loop until the count is zero
	while (lapic->timer_current_counter != 0) {
		// read the current TSC value
		final_tsc = farch_tsc_read_weak();

		// calculate the difference
		delta = final_tsc - loop_initial_tsc;

		if (delta == 0) {
			// disregard as bogus
			loop_initial_tsc = final_tsc;
			continue;
		}

		// if it's lower than the minimum, it's the new minimum
		if (delta < delta_min) {
			delta_min = delta;
		}

		// likewise for the maximum
		if (delta > delta_max) {
			delta_max = delta;
		}

		// set the current TSC value as the initial value for the next loop
		loop_initial_tsc = final_tsc;

		// ...and increment the loop count
		++loop_count;
	}

	// restore the timer configuration
	set_timer_mode(saved_mode);
	set_is_timer_masked(saved_is_masked);

	// if we didn't complete the minimum number of loops, someone interrupted us,
	// so our final poll results might be much larger than what they should be.
	// discard the results.
	if (loop_count < TSC_LOOP_MIN_COUNT) {
		fconsole_logf("LAPIC timer calibration failed; loop_count = %llu\n", loop_count);
		return UINT64_MAX;
	}

	// likewise, if the maximum delta is greater than the minimum delta multiplied by ::TSC_MIN_DELTA_COEFFICIENT,
	// then someone interrupted us and our results may be way off (e.g. maybe we were interrupted on the very last iteration).
	// discard the results.
	if (delta_max > (TSC_MIN_DELTA_COEFFICIENT * delta_min)) {
		fconsole_logf("LAPIC timer calibration failed; delta_max = %llu, delta_min = %llu\n", delta_max, delta_min);
		return UINT64_MAX;
	}

	delta = final_tsc - initial_tsc;

	tmp = LAPIC_CYCLES;
	tmp *= FARCH_PER_CPU(tsc_frequency);
	tmp /= delta;

	return (uint64_t)tmp;
};

static void lapic_timer_schedule(uint64_t delay) {
	uint8_t divisor_value = 1;
	uint64_t cycles = farch_apic_timer_ns_to_cycles(delay);

	while (cycles > UINT32_MAX && divisor_value < 8) {
		++divisor_value;
		cycles /= 2;
	}

	if (cycles > UINT32_MAX) {
		// we'll just have to fire an early interrupt and let the timers subsystem figure out how much more time is left after that
		lapic->timer_divide_configuration = 0x0b;
		lapic->timer_initial_counter = UINT32_MAX;
	} else {
		if (divisor_value == 1) {
			lapic->timer_divide_configuration = 0x0b;
		} else {
			uint8_t real_value = divisor_value - 2;
			lapic->timer_divide_configuration = (real_value & 3) | ((real_value & (1 << 2)) << 1);
		}

		lapic->timer_initial_counter = cycles;
	}
};

// the LAPIC timer also uses the TSC for timestamps
static ftimers_backend_timestamp_t lapic_timer_current_timestamp(void) {
	return farch_tsc_read_weak();
};

static uint64_t lapic_timer_delta_to_ns(ftimers_backend_timestamp_t initial, ftimers_backend_timestamp_t final) {
	return farch_tsc_offset_to_ns(final - initial);
};

static void lapic_timer_cancel(void) {
	lapic->timer_initial_counter = 0;
};

static ftimers_backend_t lapic_timer_backend = {
	.name = "lapic-timer",
	// updated later
	.precision = 0,
	.schedule = lapic_timer_schedule,
	.current_timestamp = lapic_timer_current_timestamp,
	.delta_to_ns = lapic_timer_delta_to_ns,
	.cancel = lapic_timer_cancel,
};

static uint64_t apic_current_processor_id(void) {
	unsigned int eax;
	unsigned int ebx;
	unsigned int ecx;
	unsigned int edx;

	if (__get_cpuid(1, &eax, &ebx, &ecx, &edx)) {
		return (ebx & (0xffULL << 24)) >> 24;
	} else {
		return UINT64_MAX;
	}
};

FERRO_STRUCT(farch_ioapic_node_mmio) {
	REGISTER(selector);
	REGISTER(window);
};

FERRO_STRUCT(farch_ioapic_node) {
	farch_ioapic_node_mmio_t* mmio;
	uint8_t id;
	uint8_t version;
	uint8_t redirection_entry_count;
	uint32_t gsi_base;
};

static uint32_t farch_ioapic_node_read_u32(farch_ioapic_node_t* ioapic_node, size_t index) {
	ioapic_node->mmio->selector = index;
	return ioapic_node->mmio->window;
};

static void farch_ioapic_node_write_u32(farch_ioapic_node_t* ioapic_node, size_t index, uint32_t value) {
	ioapic_node->mmio->selector = index;
	ioapic_node->mmio->window = value;
};

static uint64_t farch_ioapic_node_read_u64(farch_ioapic_node_t* ioapic_node, size_t index) {
	return ((uint64_t)farch_ioapic_node_read_u32(ioapic_node, index + 1) << 32) | ((uint64_t)farch_ioapic_node_read_u32(ioapic_node, index));
};

static void farch_ioapic_node_write_u64(farch_ioapic_node_t* ioapic_node, size_t index, uint64_t value) {
	farch_ioapic_node_write_u32(ioapic_node, index, (uint32_t)(value & 0xffffffffULL));
	farch_ioapic_node_write_u32(ioapic_node, index + 1, (uint32_t)(value >> 32));
};

FERRO_ENUM(size_t, farch_ioapic_node_mmio_index) {
	farch_ioapic_node_mmio_index_id               = 0,
	farch_ioapic_node_mmio_index_version          = 1,
	farch_ioapic_node_mmio_index_arbitration      = 2,
	farch_ioapic_node_mmio_index_redirection_base = 0x10,
};

static uint64_t cpu_count = 0;
static farch_ioapic_node_t* ioapic_nodes = NULL;
static size_t ioapic_node_count = 0;
static fcpu_t** cpu_structs = NULL;

FERRO_STRUCT(farch_apic_legacy_mapping) {
	uint32_t gsi;
	bool active_low;
	bool level_triggered;
};

/**
 * Each index in this map is a legacy IRQ number, and the value at the index indicates the Global System Interrupt (GSI) number for that legacy IRQ number.
 *
 * By default, they're mapped 1:1, but the MADT table might specify Interrupt Source Overrides (ISOs) which might change that mapping.
 */
static farch_apic_legacy_mapping_t legacy_irq_to_gsi[16] = {
	{ .gsi =  0 },
	{ .gsi =  1 },
	{ .gsi =  2 },
	{ .gsi =  3 },
	{ .gsi =  4 },
	{ .gsi =  5 },
	{ .gsi =  6 },
	{ .gsi =  7 },
	{ .gsi =  8 },
	{ .gsi =  9 },
	{ .gsi = 10 },
	{ .gsi = 11 },
	{ .gsi = 12 },
	{ .gsi = 13 },
	{ .gsi = 14 },
	{ .gsi = 15 },
};

fcpu_id_t fcpu_current_id(void) {
	return FARCH_PER_CPU(processor_id);
};

uint64_t fcpu_count(void) {
	return cpu_count;
};

fcpu_t* fcpu_current(void) {
	return FARCH_PER_CPU(current_cpu);
};

fcpu_id_t fcpu_id(fcpu_t* cpu) {
	return cpu->apic_id;
};

void farch_apic_signal_eoi(void) {
	lapic->end_of_interrupt = 0;
};

uint64_t farch_apic_processors_online = 1;

uint64_t fcpu_online_count(void) {
	return farch_apic_processors_online;
};

static void farch_apic_ipi_work_queue_handler(void* context, fint_frame_t* frame) {
	fcpu_do_work();

	farch_apic_signal_eoi();
};

static uint8_t fapic_ipi_work_queue_interrupt_number = 0;

void farch_apic_init(void) {
	fint_disable();

	facpi_madt_t* madt = (facpi_madt_t*)facpi_find_table("APIC");
	uintptr_t lapic_address = 0;
	uint64_t lapic_frequency = UINT64_MAX;
	size_t ioapic_node_index = 0;
	size_t cpu_index = 0;

	if (!supports_apic()) {
		fpanic("CPU has no APIC");
	}

	FARCH_PER_CPU(processor_id) = apic_current_processor_id();
	if (FARCH_PER_CPU(processor_id) == UINT64_MAX) {
		fpanic("Failed to determine CPU ID");
	}

	if (!madt) {
		fpanic("no MADT table found (while looking for LAPIC)");
	}

	lapic_address = madt->lapic_address;

	for (size_t offset = 0; offset < madt->header.length - offsetof(facpi_madt_t, entries); /* handled in the body */) {
		facpi_madt_entry_header_t* header = (void*)&madt->entries[offset];

		switch (header->type) {
			case facpi_madt_entry_type_processor_lapic: {
				facpi_madt_entry_processor_lapic_t* cpu = (void*)header;
				++cpu_count;
			} break;
			case facpi_madt_entry_type_lapic_override: {
				facpi_madt_entry_lapic_override_t* override = (void*)header;
				lapic_address = override->address;
			} break;
			case facpi_madt_entry_type_ioapic: {
				facpi_madt_entry_ioapic_t* ioapic_node_info = (void*)header;
				++ioapic_node_count;
			} break;
			case facpi_madt_entry_type_ioapic_iso: {
				facpi_madt_entry_ioapic_iso_t* iso = (void*)header;
				if (iso->irq_source >= sizeof(legacy_irq_to_gsi)) {
					fconsole_logf("warning: IRQ number for legacy IRQ mapping override is outside the range of 0-15 (inclusive): %u\n", iso->irq_source);
				} else if (iso->bus_source != 0) {
					fconsole_logf("warning: unknown legacy IRQ bus source: %u\n", iso->bus_source);
				} else {
					legacy_irq_to_gsi[iso->irq_source].gsi = iso->gsi;
					legacy_irq_to_gsi[iso->irq_source].active_low = (iso->flags & 2) != 0;
					legacy_irq_to_gsi[iso->irq_source].level_triggered = (iso->flags & 8) != 0;
				}
			} break;
		}

		offset += header->length;
	}

	for (size_t i = 0; i < sizeof(legacy_irq_to_gsi) / sizeof(*legacy_irq_to_gsi); ++i) {
		fconsole_logf("IOAPIC: legacy IRQ #%zu mapped to GSI #%u (active low = %s, level triggered = %s)\n", i, legacy_irq_to_gsi[i].gsi, legacy_irq_to_gsi[i].active_low ? "yes" : "no", legacy_irq_to_gsi[i].level_triggered ? "yes" : "no");
	}

	if (fmempool_allocate(sizeof(farch_ioapic_node_t) * ioapic_node_count, NULL, (void*)&ioapic_nodes) != ferr_ok) {
		fpanic("failed to allocate IOAPIC node descriptor array");
	}

	fconsole_logf("Found %llu CPU(s)\n", cpu_count);

	if (fmempool_allocate(sizeof(*cpu_structs) * cpu_count, NULL, (void*)&cpu_structs) != ferr_ok) {
		fpanic("Failed to allocate CPU struct array");
	}

	for (size_t i = 0; i < cpu_count; ++i) {
		if (fmempool_allocate(sizeof(**cpu_structs), NULL, (void*)&cpu_structs[i]) != ferr_ok) {
			fpanic("Failed to allocate CPU struct");
		}

		simple_memset(cpu_structs[i], 0, sizeof(*cpu_structs[i]));
	}

	for (size_t offset = 0; offset < madt->header.length - offsetof(facpi_madt_t, entries); /* handled in the body */) {
		facpi_madt_entry_header_t* header = (void*)&madt->entries[offset];

		switch (header->type) {
			case facpi_madt_entry_type_ioapic: {
				facpi_madt_entry_ioapic_t* ioapic_node_info = (void*)header;
				farch_ioapic_node_t* ioapic_node = &ioapic_nodes[ioapic_node_index++];
				uint32_t version_value;

				if (fpage_map_kernel_any((void*)(uintptr_t)ioapic_node_info->address, sizeof(farch_ioapic_node_mmio_t), (void*)&ioapic_node->mmio, fpage_flag_no_cache) != ferr_ok) {
					fpanic("Failed to map IOAPIC node register space");
				}

				ioapic_node->id = (farch_ioapic_node_read_u32(ioapic_node, farch_ioapic_node_mmio_index_id) >> 24) & 0x0f;

				version_value = farch_ioapic_node_read_u32(ioapic_node, farch_ioapic_node_mmio_index_version);
				ioapic_node->version = version_value & 0xff;
				ioapic_node->redirection_entry_count = ((version_value >> 16) & 0xff) + 1;

				ioapic_node->gsi_base = ioapic_node_info->gsi_base;

				fconsole_logf("IOAPIC node found: id=%u; version=%u; GSI base=%u; GSI count=%u\n", ioapic_node->id, ioapic_node->version, ioapic_node->gsi_base, ioapic_node->redirection_entry_count);
			} break;

			case facpi_madt_entry_type_processor_lapic: {
				facpi_madt_entry_processor_lapic_t* cpu = (void*)header;
				fcpu_t* cpu_info = cpu_structs[cpu_index];

				++cpu_index;

				if (cpu->flags & facpi_madt_entry_process_lapic_flag_enabled) {
					cpu_info->flags |= farch_cpu_flag_usable;
				}

				// ignore the "online capable" flag for now since we're not using ACPI yet
				// (so we can't enable processors if they're not already enabled)

				cpu_info->apic_id = cpu->apic_id;

				if (cpu_info->apic_id == FARCH_PER_CPU(processor_id)) {
					cpu_info->flags |= farch_cpu_flag_online;
					FARCH_PER_CPU(current_cpu) = cpu_info;
					cpu_info->per_cpu_data = FARCH_PER_CPU(base);
				}

				fconsole_logf("CPU found: apic_id=%llu; usable=%s; online=%s\n", cpu_info->apic_id, (cpu_info->flags & farch_cpu_flag_usable) ? "yes" : "no", (cpu_info->flags & farch_cpu_flag_online) ? "yes" : "no");
			} break;
		}

		offset += header->length;
	}

	if (fpage_map_kernel_any((void*)lapic_address, 1, (void**)&lapic, fpage_flag_no_cache) != ferr_ok) {
		fpanic("failed to map LAPIC block");
	}

	remap_and_disable_pic();

	// ignore the spurious interrupt vector
	if (farch_int_register_handler(0xff, ignore_interrupt, NULL, 0) != ferr_ok) {
		fpanic("failed to register APIC spurious interrupt vector handler (for interrupt 255)");
	}

	// enable the APIC
	// 0xff == spurious interrupt vector number; 0x100 == enable APIC
	lapic->spurious_interrupt_vector = 0x1ff;

	// 0x30 == timer interrupt number
	lapic->lvt_timer = 0x30;

	// divide by 1
	lapic->timer_divide_configuration = 0x0b;

	for (size_t i = 0; i < MAX_CALIBRATION_ATTEMPTS; ++i) {
		lapic_frequency = determine_lapic_frequency();
		if (lapic_frequency != UINT64_MAX) {
			break;
		}
	}

	if (lapic_frequency == UINT64_MAX) {
		fconsole_logf("warning: couldn't determine LAPIC timer frequency; no LAPIC timer will be available\n");
	} else {
		FARCH_PER_CPU(lapic_frequency) = lapic_frequency;
		fconsole_logf("info: LAPIC timer frequency is %lluHz\n", lapic_frequency);

		set_timer_mode(fapic_timer_mode_oneshot);

		// add one to ensure the TSC timer takes precedence (if available)
		lapic_timer_backend.precision = farch_apic_timer_cycles_to_ns(1) + 1;

		ftimers_register_backend(&lapic_timer_backend);
	}

	// setup an interrupt handler for the timer
	if (farch_int_register_handler(0x30, timer_interrupt_handler, NULL, 0) != ferr_ok) {
		fpanic("failed to register APIC timer interrupt handler (for interrupt 48)");
	}

	if (supports_tsc_deadline()) {
#if 0
		fconsole_log("info: CPU/APIC supports TSC-deadline mode; using it\n");
		set_timer_mode(fapic_timer_mode_tsc_deadline);

		tsc_deadline_backend.precision = farch_tsc_offset_to_ns(1);

		ftimers_register_backend(&tsc_deadline_backend);
#endif
	} else {
		fconsole_log("warning: CPU/APIC doesn't support TSC-deadline mode; no TSC-deadline timer will be available\n");
	}

	// register an interrupt handler for the IPI work queue
	fpanic_status(farch_int_register_next_available(farch_apic_ipi_work_queue_handler, NULL, &fapic_ipi_work_queue_interrupt_number, farch_int_handler_flag_safe_mode));

	// now initialize other processors
	void* smp_init_code = NULL;
	farch_smp_init_data_t* smp_init_data = NULL;
	fpage_table_t* smp_init_root_table = NULL;
	fpage_table_t* smp_init_p3_table = NULL;
	fpage_table_t* smp_init_p2_table = NULL;
	fpage_table_t* smp_init_p1_table = NULL;
	size_t smp_init_code_length = (size_t)(&farch_smp_init_code_end - &farch_smp_init_code_start);

	// make sure the SMP init code fits in a single page
	fassert(smp_init_code_length <= FPAGE_PAGE_SIZE);

	// first, copy the AP init code into low memory, set up some SMP init data, and set up the (stubbed) root page table
	fpanic_status(fpage_space_map_any(fpage_space_kernel(), (void*)FARCH_SMP_INIT_BASE, 1, &smp_init_code, 0));
	fpanic_status(fpage_space_map_any(fpage_space_kernel(), (void*)FARCH_SMP_INIT_DATA_BASE, 1, (void*)&smp_init_data, 0));
	fpanic_status(fpage_space_map_any(fpage_space_kernel(), (void*)FARCH_SMP_INIT_ROOT_TABLE_BASE, 1, (void*)&smp_init_root_table, 0));
	fpanic_status(fpage_space_map_any(fpage_space_kernel(), (void*)FARCH_SMP_INIT_P3_TABLE_BASE, 1, (void*)&smp_init_p3_table, 0));
	fpanic_status(fpage_space_map_any(fpage_space_kernel(), (void*)FARCH_SMP_INIT_P2_TABLE_BASE, 1, (void*)&smp_init_p2_table, 0));
	fpanic_status(fpage_space_map_any(fpage_space_kernel(), (void*)FARCH_SMP_INIT_P1_TABLE_BASE, 1, (void*)&smp_init_p1_table, 0));

	simple_memcpy(smp_init_code, &farch_smp_init_code_start, smp_init_code_length);

	// clear out the SMP init data
	simple_memset(smp_init_data, 0, sizeof(*smp_init_data));

	// stub the root page table by copying the root page table we use for this CPU
	simple_memcpy(smp_init_root_table, (void*)fpage_virtual_address_for_table(0, 0, 0, 0), sizeof(*smp_init_root_table));

	// update the recursive table pointer
	smp_init_root_table->entries[fpage_root_recursive_index] = fpage_table_entry(FARCH_SMP_INIT_ROOT_TABLE_BASE, true);

	// now identity-map the addresses we use for SMP initialization
	//
	// note that, because all our addresses are below 1MiB, they all fit within a single P1 table
	smp_init_root_table->entries[FPAGE_VIRT_L4(FARCH_SMP_INIT_BASE)] = fpage_table_entry(FARCH_SMP_INIT_P3_TABLE_BASE, true);
	smp_init_p3_table->entries[FPAGE_VIRT_L3(FARCH_SMP_INIT_BASE)] = fpage_table_entry(FARCH_SMP_INIT_P2_TABLE_BASE, true);
	smp_init_p2_table->entries[FPAGE_VIRT_L2(FARCH_SMP_INIT_BASE)] = fpage_table_entry(FARCH_SMP_INIT_P1_TABLE_BASE, true);
	smp_init_p1_table->entries[FPAGE_VIRT_L1(FARCH_SMP_INIT_BASE)] = fpage_page_entry(FARCH_SMP_INIT_BASE, true);
	smp_init_p1_table->entries[FPAGE_VIRT_L1(FARCH_SMP_INIT_DATA_BASE)] = fpage_page_entry(FARCH_SMP_INIT_DATA_BASE, true);

	// initialize the stub GDT
	smp_init_data->gdt.entries[0] = 0; // null segment
	smp_init_data->gdt.entries[1] = farch_int_gdt_flags_common | farch_int_gdt_flag_long | farch_int_gdt_flag_executable; // code segment
	smp_init_data->gdt.entries[2] = farch_int_gdt_flags_common; // data segment

	// initialize the GDT pointer (with the physical address)
	smp_init_data->gdt_pointer.limit = sizeof(smp_init_data->gdt) - 1;
	smp_init_data->gdt_pointer.base = FARCH_SMP_INIT_DATA_BASE + offsetof(farch_smp_init_data_t, gdt);

	// initialize the stub IDT pointer
	// (with a length of 0 to cause triple faults on interrupts during initialization)
	smp_init_data->idt_pointer.limit = 0;
	smp_init_data->idt_pointer.base = 0;

	for (size_t i = 0; i < cpu_count; ++i) {
		fcpu_t* cpu = cpu_structs[i];

		if (cpu == FARCH_PER_CPU(current_cpu) || (cpu->flags & farch_cpu_flag_usable) == 0) {
			continue;
		}

		// reset the "initialization done" flags
		__atomic_store_n(&smp_init_data->init_done, 0, __ATOMIC_RELAXED);
		__atomic_store_n(&smp_init_data->init_stage2_done, 0, __ATOMIC_RELEASE);

		// set the processor's APIC ID
		smp_init_data->apic_id = cpu->apic_id;

		// allocate a new init stack for this CPU
		fpanic_status(fpage_space_allocate(fpage_space_kernel(), fpage_round_up_to_page_count(FARCH_SMP_INIT_STACK_SIZE), &smp_init_data->stack, fpage_flag_prebound));

		// allocate a per-CPU data structure for this CPU
		fpanic_status(fmempool_allocate_advanced(sizeof(*cpu->per_cpu_data), 0, UINT8_MAX, fmempool_flag_prebound, NULL, (void*)&cpu->per_cpu_data));

		// allocate a root page table for this CPU
		// (the one that we'll actually use later on)
		fpanic_status(fpage_space_allocate(fpage_space_kernel(), fpage_round_up_to_page_count(sizeof(*cpu->root_table)), (void*)&cpu->root_table, fpage_flag_prebound | fpage_flag_zero));

		// zero it out
		simple_memset(cpu->per_cpu_data, 0, sizeof(*cpu->per_cpu_data));

		// set the pointer to the CPU info structure
		smp_init_data->cpu_info_struct = cpu;

		// NOTE: for now, we assume that all CPUs on the system use the same TSC and LAPIC timer frequency
		smp_init_data->tsc_frequency = FARCH_PER_CPU(tsc_frequency);
		smp_init_data->lapic_frequency = FARCH_PER_CPU(lapic_frequency);

		// ensure that all our writes are visible
		__atomic_thread_fence(__ATOMIC_RELEASE);

		// clear APIC errors
		lapic->error_status = 0;

		//
		// send an INIT IPI
		//

		// first, set the destination
		lapic->interrupt_command_32_63 = (cpu->apic_id & 0xff) << 24;

		// now set the rest of the ICR to issue the INIT
		lapic->interrupt_command_0_31 = fapic_icr_flag_trigger_mode_edge | fapic_icr_flag_level_assert | fapic_icr_flag_delivery_status_idle | fapic_icr_flag_destination_mode_physical | (fapic_lvt_delivery_mode_init << 8);

		// wait 10ms
		ftimers_delay_spin(10ull * 1000 * 1000, NULL);

		// try to issue a SIPI for the processor twice
		// first we try 1ms, then we try 1s
		for (size_t j = 0; j < 2; ++j) {
			// clear APIC errors
			lapic->error_status = 0;

			//
			// send a start-up IPI
			//

			// first, set the destination
			lapic->interrupt_command_32_63 = (cpu->apic_id & 0xff) << 24;

			// now set the rest of the ICR to issue the SIPI
			lapic->interrupt_command_0_31 = fapic_icr_flag_trigger_mode_edge | fapic_icr_flag_level_assert | fapic_icr_flag_delivery_status_idle | fapic_icr_flag_destination_mode_physical | (fapic_lvt_delivery_mode_start_up << 8) | ((FARCH_SMP_INIT_BASE >> 12) & 0xff);

			// wait
			//
			// we wait 1ms the first time around, and then we try 1 second the second time around
			if (ftimers_delay_spin(1ull * 1000 * 1000 * ((j == 1) ? 1000 : 1), &smp_init_data->init_done)) {
				// great, we're done!
				break;
			}
		}

		if (__atomic_load_n(&smp_init_data->init_done, __ATOMIC_RELAXED) == 0) {
			// we were unable to bring up this processor :(
			fconsole_logf("Unable to spin up processor with APIC ID %llu\n", cpu->apic_id);

			// go ahead and free the stack we allocated for it
			fpanic_status(fpage_space_free(fpage_space_kernel(), smp_init_data->stack, fpage_round_up_to_page_count(FARCH_SMP_INIT_STACK_SIZE)));

			// and free the per-CPU data
			fpanic_status(fmempool_free(cpu->per_cpu_data));
			cpu->per_cpu_data = NULL;

			// and the root page table
			fpanic_status(fpage_space_free(fpage_space_kernel(), cpu->root_table, fpage_round_up_to_page_count(sizeof(*cpu->root_table))));

			continue;
		}

		// wait for it to be done initializing stage 2
		while (true) {
			if (__atomic_load_n(&smp_init_data->init_stage2_done, __ATOMIC_RELAXED) != 0) {
				break;
			}
			fcpu_do_work();
			farch_lock_spin_yield();
		}

		// use `__ATOMIC_ACQUIRE` to ensure that all writes performed by the AP during initialization are visible to us now
		__atomic_thread_fence(__ATOMIC_ACQUIRE);

		fconsole_logf("Successfully spun up processor with APIC ID %llu\n", cpu->apic_id);

		cpu->flags |= farch_cpu_flag_online;
	}

	// we can now unmap the regions we mapped earlier
	fpanic_status(fpage_space_unmap(fpage_space_kernel(), smp_init_code, 1));
	fpanic_status(fpage_space_unmap(fpage_space_kernel(), smp_init_data, 1));
	fpanic_status(fpage_space_unmap(fpage_space_kernel(), smp_init_root_table, 1));
	fpanic_status(fpage_space_unmap(fpage_space_kernel(), smp_init_p3_table, 1));
	fpanic_status(fpage_space_unmap(fpage_space_kernel(), smp_init_p2_table, 1));
	fpanic_status(fpage_space_unmap(fpage_space_kernel(), smp_init_p1_table, 1));

	// TODO: continue processor initialization
	// at this point, the APs are waiting in the long-mode higher-half for us to continue setting up them.

	fint_enable();
};

void farch_apic_init_secondary_cpu(void) {
	fint_disable();

	// enable the APIC
	// 0xff == spurious interrupt vector number; 0x100 == enable APIC
	lapic->spurious_interrupt_vector = 0x1ff;

	// 0x30 == timer interrupt number
	lapic->lvt_timer = 0x30;

	// divide by 1
	lapic->timer_divide_configuration = 0x0b;

	fint_enable();
};

/**
 * Finds the IOAPIC node that manages the given GSI.
 *
 * @param[in,out] in_out_gsi_number On input, this points to the GSI number whose IOAPIC node should be found.
 *                                  On output, the GSI number relative to the IOAPIC node's base GSI will be written into it.
 *
 * @returns The IOAPIC node for the given GSI, or `NULL` if none was found.
 */
static farch_ioapic_node_t* farch_ioapic_node_for_gsi(uint32_t* in_out_gsi_number) {
	for (size_t i = 0; i < ioapic_node_count; ++i) {
		farch_ioapic_node_t* ioapic_node = &ioapic_nodes[i];
		if (ioapic_node->gsi_base <= *in_out_gsi_number && ioapic_node->gsi_base + ioapic_node->redirection_entry_count > *in_out_gsi_number) {
			*in_out_gsi_number = *in_out_gsi_number - ioapic_node->gsi_base;
			return ioapic_node;
		}
	}
	return NULL;
};

ferr_t farch_ioapic_map(uint32_t gsi_number, bool active_low, bool level_triggered, uint8_t target_vector_number) {
	farch_ioapic_node_t* ioapic_node;
	uint32_t low;
	uint32_t high;

	if (target_vector_number < 0x30 || target_vector_number == 0xff) {
		return ferr_invalid_argument;
	}

	ioapic_node = farch_ioapic_node_for_gsi(&gsi_number);
	if (!ioapic_node) {
		return ferr_invalid_argument;
	}

	low = farch_ioapic_node_read_u32(ioapic_node, farch_ioapic_node_mmio_index_redirection_base + gsi_number * 2);
	low = (low & ~(0xffULL)) | target_vector_number;
	low = (low & ~(0x7ULL << 8)) | (0ULL << 8);
	low = (low & ~(1ULL << 11)) | (0ULL << 11);
	low = (low & ~(1ULL << 13)) | ((active_low ? 1ULL : 0ULL) << 13);
	low = (low & ~(1ULL << 15)) | ((level_triggered ? 1ULL : 0ULL) << 15);
	low |= 1ULL << 16;
	farch_ioapic_node_write_u32(ioapic_node, farch_ioapic_node_mmio_index_redirection_base + gsi_number * 2, low);

	high = farch_ioapic_node_read_u32(ioapic_node, farch_ioapic_node_mmio_index_redirection_base + gsi_number * 2 +1);
	high = (high & ~(0xffULL << 24)) | ((fcpu_current_id() & 0x0fULL) << 24);
	farch_ioapic_node_write_u32(ioapic_node, farch_ioapic_node_mmio_index_redirection_base + gsi_number * 2 + 1, high);

	return ferr_ok;
};

ferr_t farch_ioapic_mask(uint32_t gsi_number) {
	farch_ioapic_node_t* ioapic_node = farch_ioapic_node_for_gsi(&gsi_number);
	if (!ioapic_node) {
		return ferr_invalid_argument;
	}

	uint32_t low = farch_ioapic_node_read_u32(ioapic_node, farch_ioapic_node_mmio_index_redirection_base + gsi_number * 2);
	low |= 1ULL << 16;
	farch_ioapic_node_write_u32(ioapic_node, farch_ioapic_node_mmio_index_redirection_base + gsi_number * 2, low);

	return ferr_ok;
};

ferr_t farch_ioapic_unmask(uint32_t gsi_number) {
	farch_ioapic_node_t* ioapic_node = farch_ioapic_node_for_gsi(&gsi_number);
	if (!ioapic_node) {
		return ferr_invalid_argument;
	}

	uint32_t low = farch_ioapic_node_read_u32(ioapic_node, farch_ioapic_node_mmio_index_redirection_base + gsi_number * 2);
	low &= ~(1ULL << 16);
	farch_ioapic_node_write_u32(ioapic_node, farch_ioapic_node_mmio_index_redirection_base + gsi_number * 2, low);

	return ferr_ok;
};

ferr_t farch_ioapic_mask_legacy(uint8_t legacy_irq_number) {
	if (legacy_irq_number >= 16) {
		return ferr_invalid_argument;
	}
	return farch_ioapic_mask(legacy_irq_to_gsi[legacy_irq_number].gsi);
};

ferr_t farch_ioapic_unmask_legacy(uint8_t legacy_irq_number) {
	if (legacy_irq_number >= 16) {
		return ferr_invalid_argument;
	}
	return farch_ioapic_unmask(legacy_irq_to_gsi[legacy_irq_number].gsi);
};

ferr_t farch_ioapic_map_legacy(uint8_t legacy_irq_number, uint8_t target_vector_number) {
	if (legacy_irq_number >= 16) {
		return ferr_invalid_argument;
	}

	return farch_ioapic_map(legacy_irq_to_gsi[legacy_irq_number].gsi, legacy_irq_to_gsi[legacy_irq_number].active_low, legacy_irq_to_gsi[legacy_irq_number].level_triggered, target_vector_number);
};

ferr_t fcpu_arch_interrupt_all(bool include_current) {
	lapic->error_status = 0;
	lapic->interrupt_command_0_31 =
		((uint32_t)(include_current ? fapic_icr_destination_shorthand_all : fapic_icr_destination_shorthand_all_except_self) << 18) |
		fapic_icr_flag_trigger_mode_edge         |
		fapic_icr_flag_level_assert              |
		fapic_icr_flag_delivery_status_idle      |
		fapic_icr_flag_destination_mode_physical |
		(fapic_lvt_delivery_mode_fixed << 8)     |
		fapic_ipi_work_queue_interrupt_number
		;
	return ferr_ok;
};

ferr_t farch_apic_interrupt_cpu(fcpu_t* cpu, uint8_t vector_number) {
	lapic->error_status = 0;
	lapic->interrupt_command_32_63 = (cpu->apic_id & 0xff) << 24;
	lapic->interrupt_command_0_31 =
		((uint32_t)(fapic_icr_destination_shorthand_none) << 18) |
		fapic_icr_flag_trigger_mode_edge         |
		fapic_icr_flag_level_assert              |
		fapic_icr_flag_delivery_status_idle      |
		fapic_icr_flag_destination_mode_physical |
		(fapic_lvt_delivery_mode_fixed << 8)     |
		vector_number
		;
	return ferr_ok;
};
