/*
 * This file is part of Anillo OS
 * Copyright (C) 2020 Anillo OS Developers
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

#ifndef _FERRO_CORE_AARCH64_GIC_H_
#define _FERRO_CORE_AARCH64_GIC_H_

#include <ferro/base.h>
#include <ferro/error.h>
#include <ferro/core/aarch64/interrupts.h>

FERRO_DECLARATIONS_BEGIN;

/**
 * A handler that is to be called when an interrupt is received.
 *
 * The handler ***is*** allowed to modify the given frame, which may alter the state of the processor upon return.
 *
 * The handler is called with interrupts disabled.
 */
typedef void (*farch_gic_interrupt_handler_f)(farch_int_exception_frame_t* frame);

/**
 * Initializes the AARCH64 Generic Interrupt Controller subsystem.
 */
void farch_gic_init(void);

/**
 * Registers the given handler for the given interrupt number.
 *
 * @param interrupt   The interrupt number to register the handler for.
 * @param for_group_0 Whether the interrupt is on group 0 or not.
 * @param handler     The handler to call when the interrupt is received. See `farch_gic_interrupt_handler_f` for more details.
 *
 * Return values:
 * @retval ferr_ok               The handler was registered successfully.
 * @retval ferr_invalid_argument One or more of: 1) the given interrupt number is outside the permitted range (0-1019, inclusive), 2) the handler is `NULL`.
 * @retval ferr_temporary_outage A handler for the given interrupt is already registered and must be explicitly unregistered with `farch_gic_unregister_handler`.
 */
FERRO_WUR ferr_t farch_gic_register_handler(uint64_t interrupt, bool for_group_0, farch_gic_interrupt_handler_f handler);

/**
 * Unregisters the handler for the given interrupt number.
 *
 * @param interrupt The interrupt number for which to unregister the handler.
 * @param for_group_0 Whether the interrupt is on group 0 or not.
 *
 * Returns values:
 * @retval ferr_ok               The handler for the interrupt was successfully unregistered.
 * @retval ferr_invalid_argument The given interrupt number is outside the permitted range (0-1019, inclusive).
 * @retval ferr_no_such_resource There is no handler registered for the given interrupt number. This is also returned when there is a handler registered for group 1 but the call is for a group 0 handler (and vice versa).
 */
FERRO_WUR ferr_t farch_gic_unregister_handler(uint64_t interrupt, bool for_group_0);

/**
 * Sets the priority of the given interrupt.
 *
 * @param interrupt The number of the interrupt on which to operate.
 * @param priority  The new priority of the given interrupt.
 *
 * Return values:
 * @retval ferr_ok               The given interrupt's priority has been successfully modified.
 * @retval ferr_invalid_argument The given interrupt number is outside the permitted range (0-1019, inclusive).
 */
FERRO_WUR ferr_t farch_gic_interrupt_priority_write(uint64_t interrupt, uint8_t priority);

/**
 * Checks whether the given interrupt is enabled.
 *
 * @param interrupt   The number of the interrupt on which to operate.
 * @param out_enabled A pointer in which the result will be written. May be `NULL`.
 *
 * Return values:
 * @retval ferr_ok               The given interrupt's current enablement status has been successfully retrieved.
 * @retval ferr_invalid_argument The given interrupt number is outside the permitted range (0-1019, inclusive).
 */
FERRO_WUR ferr_t farch_gic_interrupt_enabled_read(uint64_t interrupt, bool* out_enabled);

/**
 * Sets whether the given interrupt is enabled.
 *
 * @param interrupt The number of the interrupt on which to operate.
 * @param enabled   Whether the interrupt should be enabled or not.
 *
 * Return values:
 * @retval ferr_ok               The given interrupt's current enablement status has been successfully modified.
 * @retval ferr_invalid_argument The given interrupt number is outside the permitted range (0-1019, inclusive).
 */
FERRO_WUR ferr_t farch_gic_interrupt_enabled_write(uint64_t interrupt, bool enabled);

/**
 * Checks whether the given interrupt is pending.
 *
 * @param interrupt   The number of the interrupt on which to operate.
 * @param out_pending A pointer in which the result will be written. May be `NULL`.
 *
 * Return values:
 * @retval ferr_ok               The given interrupt's current pending status has been successfully retrieved.
 * @retval ferr_invalid_argument The given interrupt number is outside the permitted range (0-1019, inclusive).
 */
FERRO_WUR ferr_t farch_gic_interrupt_pending_read(uint64_t interrupt, bool* out_pending);

/**
 * Sets whether the given interrupt is pending.
 *
 * @param interrupt The number of the interrupt on which to operate.
 * @param pending   Whether the interrupt should be pending or not.
 *
 * Return values:
 * @retval ferr_ok               The given interrupt's current pending status has been successfully modified.
 * @retval ferr_invalid_argument The given interrupt number is outside the permitted range (0-1019, inclusive).
 */
FERRO_WUR ferr_t farch_gic_interrupt_pending_write(uint64_t interrupt, bool pending);

/**
 * Sets the given interrupt's target core ID.
 *
 * @param interrupt The number of the interrupt on which to operate.
 * @param core      The core ID to direct the interrupt to.
 *
 * Return values:
 * @retval ferr_ok               The given interrupt's target core ID has been successfully modified.
 * @retval ferr_invalid_argument The given interrupt number is outside the permitted range (0-1019, inclusive).
 */
FERRO_WUR ferr_t farch_gic_interrupt_target_core_write(uint64_t interrupt, uint8_t core);

FERRO_OPTIONS(uint8_t, farch_gic_interrupt_configuration) {
	farch_gic_interrupt_configuration_level_triggered = 0 << 1,
	farch_gic_interrupt_configuration_edge_triggered  = 1 << 1,
};

/**
 * Sets the given interrupt's configuration.
 *
 * @param interrupt     The number of the interrupt on which to operate.
 * @param configuration The configuration to set for the interrupt.
 *
 * Return values:
 * @retval ferr_ok               The given interrupt's configuration has been successfully modified.
 * @retval ferr_invalid_argument The given interrupt number is outside the permitted range (0-1019, inclusive).
 */
FERRO_WUR ferr_t farch_gic_interrupt_configuration_write(uint64_t interrupt, farch_gic_interrupt_configuration_t configuration);

/**
 * Checks the given interrupt's current group membership.
 *
 * @param interrupt      The number of the interrupt on which to operate.
 * @param out_is_group_0 A pointer in which the result will be written. May be `NULL`.
 *
 * Return values:
 * @retval ferr_ok               The given interrupt's current group membership has been successfully retrieved.
 * @retval ferr_invalid_argument The given interrupt number is outside the permitted range (0-1019, inclusive).
 */
FERRO_WUR ferr_t farch_gic_interrupt_group_read(uint64_t interrupt, bool* out_is_group_0);

/**
 * Sets the given interrupt's group membership.
 *
 * @param interrupt  The number of the interrupt on which to operate.
 * @param is_group_0 If `true`, the interrupt should belong to group 0. Otherwise, it should belong to group 1.
 *
 * Return values:
 * @retval ferr_ok               The given interrupt's group membership has been successfully modified.
 * @retval ferr_invalid_argument The given interrupt number is outside the permitted range (0-1019, inclusive).
 * @retval ferr_unsupported      The given interrupt's group membership cannot be changed. This is only returned if the interrupt's current membership is not the desired one AND its membership cannot be modified.
 */
FERRO_WUR ferr_t farch_gic_interrupt_group_write(uint64_t interrupt, bool is_group_0);

FERRO_ALWAYS_INLINE uint8_t farch_gic_current_core_id(void) {
	uint64_t value;
	__asm__ volatile("mrs %0, mpidr_el1" : "=r" (value));
	return (value >> 8) & 0xff;
};

FERRO_DECLARATIONS_END;

#endif // _FERRO_CORE_AARCH64_GIC_H_
