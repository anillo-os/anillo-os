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

#ifndef _LIBSYS_COUNTERS_H_
#define _LIBSYS_COUNTERS_H_

#include <stdint.h>
#include <stddef.h>

#include <libsys/base.h>
#include <libsys/objects.h>
#include <libsys/timeout.h>

LIBSYS_DECLARATIONS_BEGIN;

LIBSYS_OBJECT_CLASS(counter);

/**
 * Counter values are limited to the low 63 bits.
 *
 * The most significant bit is reserved internally and ignored on input
 * and zero on output.
 */
typedef uint64_t sys_counter_value_t;

LIBSYS_WUR ferr_t sys_counter_create(sys_counter_value_t initial_value, sys_counter_t** out_counter);
sys_counter_value_t sys_counter_value(sys_counter_t* counter);
void sys_counter_increment(sys_counter_t* counter);
void sys_counter_set(sys_counter_t* counter, sys_counter_value_t value);
void sys_counter_wait(sys_counter_t* counter, uint64_t timeout, sys_timeout_type_t timeout_type);
void sys_counter_wait_value(sys_counter_t* counter, sys_counter_value_t target_value, uint64_t timeout, sys_timeout_type_t timeout_type);

LIBSYS_DECLARATIONS_END;

#endif // _LIBSYS_COUNTERS_H_
