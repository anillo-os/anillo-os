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
 * CPU subsystem.
 */

#ifndef _FERRO_CORE_CPU_H_
#define _FERRO_CORE_CPU_H_

#include <stdint.h>
#include <stdbool.h>

#include <ferro/base.h>
#include <ferro/error.h>

FERRO_DECLARATIONS_BEGIN;

/**
 * @addtogroup CPU
 *
 * The CPU subsystem.
 *
 * @{
 */

typedef uint64_t fcpu_id_t;

FERRO_STRUCT_FWD(fcpu);

typedef void (*fcpu_interrupt_work_f)(void* context);

// these are arch-dependent functions that each architecture is expected to implement

/**
 * Retrieves the CPU info structure for the current CPU.
 */
fcpu_t* fcpu_current(void);

/**
 * Retrieves the ID of the current processor.
 *
 * @note This is guaranteed to be unique for the entire system.
 */
fcpu_id_t fcpu_current_id(void);

fcpu_id_t fcpu_id(fcpu_t* cpu);

/**
 * Retrieves the total number of available CPUs, including any that have been disabled.
 *
 * This number corresponds to the number of total unique CPU IDs.
 * If a single physical CPU presents itself as two logical CPUs AND each one has its own ID, then this function would return `2`.
 * If, instead, it presents itself as two logical CPUs BUT they both have the same ID, then this function would return `1`.
 */
uint64_t fcpu_count(void);

uint64_t fcpu_online_count(void);

/**
 * Interrupts all online CPUs (or all except the current) and executes the given work function
 * on them.
 *
 * If @p wait is true, this function will not return until the work function has finished executing
 * on all interrupted CPUs.
 *
 * @note If @p wait is false, some memory may need to be allocated to fulfill the request.
 *
 * @note The work is NOT guaranteed to run in an interrupt context.
 */
FERRO_WUR ferr_t fcpu_interrupt_all(fcpu_interrupt_work_f work, void* context, bool include_current, bool wait);

/**
 * @}
 */

FERRO_DECLARATIONS_END;

#endif // _FERRO_CORE_CPU_H_
