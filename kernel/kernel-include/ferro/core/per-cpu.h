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
 * Per-CPU data subsystem.
 */

#ifndef _FERRO_CORE_PER_CPU_H_
#define _FERRO_CORE_PER_CPU_H_

#include <stdint.h>
#include <stdbool.h>

#include <ferro/base.h>
#include <ferro/error.h>

FERRO_DECLARATIONS_BEGIN;

typedef uint64_t fper_cpu_key_t;
typedef uintptr_t fper_cpu_data_t;

/**
 * @note Destructors are invoked synchronously from whatever context the per-CPU data is being destroyed from.
 *       Therefore, it is wise to only perform interrupt-safe tasks in them and, if possible, schedule most or all of the work to a worker.
 */
typedef void (*fper_cpu_data_destructor_f)(void* context, fper_cpu_data_t data);

/**
 * Registers for a new per-CPU data key.
 *
 * The returned key can then be used with the rest of the fper_cpu functions to manage the data associated with it.
 *
 * @param[out] out_key A pointer in which the new key will be written.
 *
 * @retval ferr_ok               The key was successfully created and written into @p out_key.
 * @retval ferr_temporary_outage There were insufficient resources to create the new key.
 * @retval ferr_invalid_argument @p out_key was `NULL`.
 */
FERRO_WUR ferr_t fper_cpu_register(fper_cpu_key_t* out_key);

/**
 * Unregisters the given per-CPU data key.
 *
 * After a successful call to this function with a given key, the key is now invalid and may not be passed to any other fper_cpu functions.
 *
 * @param key                      The key to unregister.
 * @param skip_previous_destructor If `true`, the destructor for the data previously associated with the given key (if any) will not be invoked.
 *
 * @retval ferr_ok               The key was successfully unregistered.
 * @retval ferr_invalid_argument @p key was not a valid key.
 */
FERRO_WUR ferr_t fper_cpu_unregister(fper_cpu_key_t key, bool skip_previous_destructor);

/**
 * Reads the data for the current CPU associated with the given key.
 *
 * @param           key The key whose associated data will be read.
 * @param[out] out_data A pointer into which the data will be read.
 *
 * @retval ferr_ok               The key's associated data for the current CPU was successfully read.
 * @retval ferr_no_such_resource The key had no associated data for the current CPU.
 * @retval ferr_invalid_argument @p key was not a valid key.
 */
FERRO_WUR ferr_t fper_cpu_read(fper_cpu_key_t key, fper_cpu_data_t* out_data);

/**
 * Associates the given data with the current CPU and the given key.
 *
 * @param key                      The key whose associated data will be written.
 * @param data                     The data to associate.
 * @param destructor               An optional destructor for the given data.
 *                                 It will be called when the data is cleared or overwritten (if not explicitly skipped).
 * @param destructor_context       An optional context argument for the destructor.
 * @param skip_previous_destructor If `true`, the destructor for the data previously associated with the given key (if any) will not be invoked.
 *
 * @retval ferr_ok               The given data has been successfully associated with the current CPU and the given key.
 * @retval ferr_invalid_argument @p key was not a valid key.
 */
FERRO_WUR ferr_t fper_cpu_write(fper_cpu_key_t key, fper_cpu_data_t data, fper_cpu_data_destructor_f destructor, void* destructor_context, bool skip_previous_destructor);

/**
 * Clears the data associated with the current CPU and the given key.
 *
 * @param key                      The key whose associated data will be cleared.
 * @param skip_previous_destructor If `true`, the destructor for the data previously associated with the given key (if any) will not be invoked.
 *
 * @retval ferr_ok               There was previously data associated with the current CPU and the given key and it has now been cleared.
 * @retval ferr_no_such_resource There was no data associated with the current CPU and the given key.
 * @retval ferr_invalid_argument @p key was not a valid key.
 */
FERRO_WUR ferr_t fper_cpu_clear(fper_cpu_key_t key, bool skip_previous_destructor);

FERRO_DECLARATIONS_END;

#endif // _FERRO_CORE_PER_CPU_H_
