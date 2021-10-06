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

#define HZ_PER_KHZ 1000
#define MAX_CALIBRATION_ATTEMPTS 10
// XXX: this is kind of arbitrary
#define LAPIC_CYCLES 500000
// TODO: determine this
#define TSC_LOOP_MIN_COUNT 1
// XXX: this is also kind of arbitrary
#define TSC_MIN_DELTA_COEFFICIENT 10

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
	fapic_lvt_delivery_mode_fixed  = 0,
	fapic_lvt_delivery_mode_smi    = 2,
	fapic_lvt_delivery_mode_nmi    = 4,
	fapic_lvt_delivery_mode_init   = 5,
	fapic_lvt_delivery_mode_extint = 7,
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

static fapic_block_t* lapic = NULL;

static void ignore_interrupt(fint_frame_t* frame) {};

static void remap_and_disable_pic(void) {
	for (size_t i = 0x20; i < 0x30; ++i) {
		if (farch_int_register_handler(i, ignore_interrupt) != ferr_ok) {
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
static void timer_interrupt_handler(fint_frame_t* frame) {
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
		//fconsole_logf("LAPIC timer calibration failed; loop_count = %zu\n", loop_count);
		return UINT64_MAX;
	}

	// likewise, if the maximum delta is greater than the minimum delta multiplied by ::TSC_MIN_DELTA_COEFFICIENT,
	// then someone interrupted us and our results may be way off (e.g. maybe we were interrupted on the very last iteration).
	// discard the results.
	if (delta_max > (TSC_MIN_DELTA_COEFFICIENT * delta_min)) {
		//fconsole_logf("LAPIC timer calibration failed; delta_max = %zu, delta_min = %zu\n", delta_max, delta_min);
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

uint64_t fcpu_id(void) {
	return FARCH_PER_CPU(processor_id);
};

uint64_t fcpu_count(void) {
	return cpu_count;
};

void farch_apic_signal_eoi(void) {
	lapic->end_of_interrupt = 0;
};

void farch_apic_init(void) {
	facpi_madt_t* madt = (facpi_madt_t*)facpi_find_table("APIC");
	uintptr_t lapic_address = 0;
	uint64_t lapic_frequency = UINT64_MAX;
	size_t ioapic_node_index = 0;

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

	if (fmempool_allocate(sizeof(farch_ioapic_node_t) * ioapic_node_count, NULL, (void*)&ioapic_nodes) != ferr_ok) {
		fpanic("failed to allocate IOAPIC node descriptor array");
	}

	for (size_t offset = 0; offset < madt->header.length - offsetof(facpi_madt_t, entries); /* handled in the body */) {
		facpi_madt_entry_header_t* header = (void*)&madt->entries[offset];

		switch (header->type) {
			case facpi_madt_entry_type_ioapic: {
				facpi_madt_entry_ioapic_t* ioapic_node_info = (void*)header;
				farch_ioapic_node_t* ioapic_node = &ioapic_nodes[ioapic_node_index++];
				uint32_t version_value;

				if (fpage_map_kernel_any((void*)(uintptr_t)ioapic_node_info->address, sizeof(farch_ioapic_node_mmio_t), (void*)&ioapic_node->mmio, fpage_page_flag_no_cache) != ferr_ok) {
					fpanic("Failed to map IOAPIC node register space");
				}

				ioapic_node->id = (farch_ioapic_node_read_u32(ioapic_node, farch_ioapic_node_mmio_index_id) >> 24) & 0x0f;

				version_value = farch_ioapic_node_read_u32(ioapic_node, farch_ioapic_node_mmio_index_version);
				ioapic_node->version = version_value & 0xff;
				ioapic_node->redirection_entry_count = ((version_value >> 16) & 0xff) + 1;

				ioapic_node->gsi_base = ioapic_node_info->gsi_base;
			} break;
		}

		offset += header->length;
	}

	if (fpage_map_kernel_any((void*)lapic_address, 1, (void**)&lapic, fpage_page_flag_no_cache) != ferr_ok) {
		fpanic("failed to map LAPIC block");
	}

	remap_and_disable_pic();

	// ignore the spurious interrupt vector
	if (farch_int_register_handler(0xff, ignore_interrupt) != ferr_ok) {
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
		fconsole_logf("info: LAPIC timer frequency is %luHz\n", lapic_frequency);

		set_timer_mode(fapic_timer_mode_oneshot);

		// add one to ensure the TSC timer takes precedence (if available)
		lapic_timer_backend.precision = farch_apic_timer_cycles_to_ns(1) + 1;

		ftimers_register_backend(&lapic_timer_backend);
	}

	// setup an interrupt handler for the timer
	if (farch_int_register_handler(0x30, timer_interrupt_handler) != ferr_ok) {
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
	high = (high & ~(0xffULL << 24)) | ((fcpu_id() & 0x0fULL) << 24);
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