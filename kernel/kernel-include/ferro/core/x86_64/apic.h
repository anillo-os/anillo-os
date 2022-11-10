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
 * x86_64 APIC subsystem.
 *
 * Provides 2 backends for timers subsystem.
 */

#ifndef _FERRO_CORE_X86_64_APIC_H_
#define _FERRO_CORE_X86_64_APIC_H_

#include <stdbool.h>

#include <ferro/base.h>
#include <ferro/error.h>
#include <ferro/core/per-cpu.private.h>

FERRO_DECLARATIONS_BEGIN;

/**
 * @addtogroup APIC
 *
 * The x86_64 APIC subsystem.
 *
 * @{
 */

/**
 * Initializes the APIC subsystem.
 */
void farch_apic_init(void);

void farch_apic_init_secondary_cpu(void);

/**
 * Converts the given number of nanoseconds into a number of APIC timer cycles (with a divider of 1).
 */
FERRO_ALWAYS_INLINE uint64_t farch_apic_timer_ns_to_cycles(uint64_t ns) {
	// this is terribly unoptimized, but let's trust the compiler to do the right thing
	__uint128_t tmp = ns;

	tmp *= FARCH_PER_CPU(lapic_frequency);

	// there are 1e9 nanoseconds in a second
	tmp /= 1000000000ULL;

	return (uint64_t)tmp;
};

/**
 * Converts the given number of APIC timer cycles into a number of nanoseconds.
 */
FERRO_ALWAYS_INLINE uint64_t farch_apic_timer_cycles_to_ns(uint64_t offset) {
	// again, terribly unoptimized
	__uint128_t tmp = offset;

	tmp *= 1000000000ULL;

	tmp /= FARCH_PER_CPU(lapic_frequency);

	return (uint64_t)tmp;
};

/**
 * Tells the local APIC that you've finished processing the most recent interrupt.
 */
void farch_apic_signal_eoi(void);

/**
 * Tells the IOAPIC to map the given Global System Interrupt (GSI) to the given interrupt vector.
 *
 * @param gsi_number           The number that identifies the desired GSI.
 * @param active_low           If `true`, the interrupt is processed as being active when input is low. Otherwise, if `false`, it is processed as being active when input is high.
 * @param level_triggered      If `true`, the interrupt is processed as being level triggered. Otherwise, if `false`, it is processed as being edge triggered.
 * @param target_vector_number The number that identifies the target interrupt vector.
 *
 * @note By default, when an interrupt is mapped, it is masked. To enable interrupt generation for it, it must be unmasked with farch_ioapic_unmask().
 *
 * @retval ferr_ok               The GSI was successfully mapped.
 * @retval ferr_invalid_argument One or more of: 1) @p gsi_number was outside the range supported by the system, or 2) @p target_vector_number was outside the permitted range (48-254, inclusive).
 */
ferr_t farch_ioapic_map(uint32_t gsi_number, bool active_low, bool level_triggered, uint8_t target_vector_number);

/**
 * Tells the IOAPIC not to generate interrupts when the given Global System Interrupt (GSI) is active.
 *
 * @param gsi_number The number that identifies the desired GSI.
 *
 * @retval ferr_ok               The GSI was successfully masked.
 * @retval ferr_invalid_argument @p gsi_number was outside the range supported by the system.
 */
ferr_t farch_ioapic_mask(uint32_t gsi_number);

/**
 * Tells the IOAPIC to generate interrupts when the given Global System Interrupt (GSI) is active.
 *
 * @param gsi_number The number that identifies the desired GSI.
 *
 * @retval ferr_ok               The GSI was successfully unmasked.
 * @retval ferr_invalid_argument @p gsi_number was outside the range supported by the system.
 */
ferr_t farch_ioapic_unmask(uint32_t gsi_number);

/**
 * Tells the IOAPIC not to generate interrupts when the given legacy IRQ is active.
 *
 * @param legacy_irq_number The number that identifies the desired legacy IRQ.
 *
 * @retval ferr_ok               The legacy IRQ was successfully masked.
 * @retval ferr_invalid_argument @p legacy_irq_number was outside the permitted range (0-15, inclusive).
 */
ferr_t farch_ioapic_mask_legacy(uint8_t legacy_irq_number);

/**
 * Tells the IOAPIC to generate interrupts when the given legacy IRQ is active.
 *
 * @param legacy_irq_number The number that identifies the desired legacy IRQ.
 *
 * @retval ferr_ok               The legacy IRQ was successfully unmasked.
 * @retval ferr_invalid_argument @p legacy_irq_number was outside the permitted range (0-15, inclusive).
 */
ferr_t farch_ioapic_unmask_legacy(uint8_t legacy_irq_number);

/**
 * Tells the IOAPIC to map the given legacy IRQ to the given interrupt vector.
 *
 * @param legacy_irq_number    The number that identifies the desired legacy IRQ.
 * @param target_vector_number The number that identifies the target interrupt vector.
 *
 * @note Just like farch_ioapic_map(), the interrupt is masked by default. To enable interrupt generation for it, it must be unmasked with farch_ioapic_unmask().
 *
 * @retval ferr_ok                               The legacy IRQ was mapped successfully.
 * @retval ferr_invalid_argument One or more of: 1) @p legacy_irq_number was outside the permitted range (0-15, inclusive), or 2) @p target_vector_number was outside the permitted range (48-254, inclusive).
 */
ferr_t farch_ioapic_map_legacy(uint8_t legacy_irq_number, uint8_t target_vector_number);

FERRO_WUR ferr_t farch_apic_interrupt_cpu(fcpu_t* cpu, uint8_t vector_number);

/**
 * @}
 */

FERRO_DECLARATIONS_END;

#endif // _FERRO_CORE_X86_64_APIC_H_
