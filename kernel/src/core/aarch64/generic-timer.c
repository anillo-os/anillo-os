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
 * AARCH64 Generic Timer management and timers subsystem backend.
 */

#include <ferro/core/aarch64/generic-timer.h>
#include <ferro/core/timers.private.h>
#include <ferro/core/acpi.h>
#include <ferro/core/aarch64/gic.h>
#include <ferro/core/panic.h>
#include <ferro/core/console.h>

static uint64_t generic_timer_interrupt_number = 0;

static void generic_timer_schedule(uint64_t delay_ns) {
	__asm__ volatile(
		"msr cntp_cval_el0, %0\n"
		"msr cntp_ctl_el0, %1\n"
		::
		"r" (farch_generic_timer_read_counter_weak() + farch_generic_timer_ns_to_offset(delay_ns)),
		"r" ((uint64_t)1)
	);
};

static ftimers_backend_timestamp_t generic_timer_current_timestamp(void) {
	return farch_generic_timer_read_counter_weak();
};

static uint64_t generic_timer_delta_to_ns(ftimers_backend_timestamp_t initial, ftimers_backend_timestamp_t final) {
	return farch_generic_timer_offset_to_ns(final - initial);
};

static void generic_timer_cancel(void) {
	__asm__ volatile("msr cntp_ctl_el0, %0" :: "r" ((uint64_t)2));
};

static ftimers_backend_t generic_timer_backend = {
	.name = "generic-timer",
	// filled in later
	.precision = 0,
	.schedule = generic_timer_schedule,
	.current_timestamp = generic_timer_current_timestamp,
	.delta_to_ns = generic_timer_delta_to_ns,
	.cancel = generic_timer_cancel,
};

static void generic_timer_interrupt_handler(farch_int_exception_frame_t* frame) {
	ftimers_backend_fire();
};

void farch_generic_timer_init(void) {
	facpi_gtdt_t* gtdt = (void*)facpi_find_table("GTDT");

	if (!gtdt) {
		fpanic("No GTDT ACPI table found");
	}

	fconsole_logf("info: Generic timer frequency is %luHz\n", farch_generic_timer_read_frequency());

	generic_timer_interrupt_number = gtdt->non_secure_el1_gsiv;

	generic_timer_backend.precision = farch_generic_timer_offset_to_ns(1);

	if (farch_gic_interrupt_priority_write(generic_timer_interrupt_number, 0) != ferr_ok) {
		fpanic("Failed to set timer interrupt priority");
	}

	if (farch_gic_interrupt_target_core_write(generic_timer_interrupt_number, farch_gic_current_core_id()) != ferr_ok) {
		fpanic("Failed to set timer interrupt target core");
	}

	if (farch_gic_interrupt_configuration_write(generic_timer_interrupt_number, farch_gic_interrupt_configuration_edge_triggered) != ferr_ok) {
		fpanic("Failed to set timer interrupt configuration");
	}

	if (farch_gic_interrupt_pending_write(generic_timer_interrupt_number, false) != ferr_ok) {
		fpanic("Failed to clear timer interrupt pending status");
	}

	if (farch_gic_interrupt_group_write(generic_timer_interrupt_number, true) != ferr_ok) {
		fpanic("Failed to set timer interrupt group to 0");
	}

	if (farch_gic_register_handler(generic_timer_interrupt_number, true, generic_timer_interrupt_handler) != ferr_ok) {
		fpanic("Failed to register timer interrupt handler");
	}

	if (farch_gic_interrupt_enabled_write(generic_timer_interrupt_number, true) != ferr_ok) {
		fpanic("Failed to enable timer interrupt");
	}

	ftimers_register_backend(&generic_timer_backend);
};
