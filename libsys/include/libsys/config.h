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

#ifndef _LIBSYS_CONFIG_H_
#define _LIBSYS_CONFIG_H_

#include <stdint.h>
#include <stddef.h>

#include <libsys/base.h>
#include <ferro/error.h>

LIBSYS_DECLARATIONS_BEGIN;

LIBSYS_ENUM(uint64_t, sys_config_key) {
	/**
	 * The minimum number of bytes required for a stack.
	 *
	 * **Type**: `uint64_t`
	 */
	sys_config_key_minimum_stack_size = 1,

	/**
	 * The size of each page of memory, in bytes.
	 *
	 * **Type**: `uint64_t`
	 */
	sys_config_key_page_size = 2,

	/**
	 * The total size (including padding) of a thread context structure, in bytes.
	 *
	 * **Type**: `uint64_t`
	 */
	sys_config_key_total_thread_context_size = 3,

	/**
	 * The minimum required alignment of a thread context structure, as a power of 2.
	 *
	 * **Type**: `uint8_t`
	 */
	sys_config_key_minimum_thread_context_alignment_power = 4,
};

LIBSYS_WUR ferr_t sys_config_read(sys_config_key_t key, void* buffer, size_t buffer_size, size_t* out_written_count);

//
// convenience wrappers
//

uint64_t sys_config_read_minimum_stack_size(void);
uint64_t sys_config_read_page_size(void);
uint64_t sys_config_read_total_thread_context_size(void);
uint8_t sys_config_read_minimum_thread_context_alignment_power(void);

LIBSYS_DECLARATIONS_END;

#endif // _LIBSYS_CONFIG_H_
