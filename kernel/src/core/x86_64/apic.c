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
#include <ferro/core/x86_64/interrupts.h>
#include <ferro/core/timers.private.h>
#include <ferro/core/x86_64/tsc.h>
#include <ferro/core/x86_64/msr.h>
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

static void ignore_interrupt(farch_int_isr_frame_t* frame) {};

static void remap_and_disable_pic(void) {
	for (size_t i = 0x20; i < 0x30; ++i) {
		if (farch_int_register_handler(i, ignore_interrupt) != ferr_ok) {
			fpanic("failed to register PIC interrupt handler for %zu", i);
		}
	}

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
static void timer_interrupt_handler(farch_int_isr_frame_t* frame) {
	// signal EOI here instead of after because it may never return here
	lapic->end_of_interrupt = 0;
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

static uint64_t cpu_count = 0;

uint64_t fcpu_id(void) {
	return FARCH_PER_CPU(processor_id);
};

uint64_t fcpu_count(void) {
	return cpu_count;
};

void farch_apic_init(void) {
	facpi_madt_t* madt = (facpi_madt_t*)facpi_find_table("APIC");
	uintptr_t lapic_address = 0;
	uint64_t lapic_frequency = UINT64_MAX;

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
		fconsole_log("info: CPU/APIC supports TSC-deadline mode; using it\n");
		set_timer_mode(fapic_timer_mode_tsc_deadline);

		tsc_deadline_backend.precision = farch_tsc_offset_to_ns(1);

		ftimers_register_backend(&tsc_deadline_backend);
	} else {
		fconsole_log("warning: CPU/APIC doesn't support TSC-deadline mode; no TSC-deadline timer will be available\n");
	}
};
