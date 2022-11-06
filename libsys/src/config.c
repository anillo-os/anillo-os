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

#include <libsys/config.h>
#include <libsys/abort.h>
#include <libsys/once.h>
#include <ferro/api.h>
#include <gen/libsyscall/syscall-wrappers.h>

static sys_once_t once_token = SYS_ONCE_INITIALIZER;
static ferro_constants_t constants;

static void do_init_config(void* context) {
	if (libsyscall_wrapper_constants(&constants) != ferr_ok) {
		sys_abort();
	}
};

static void ensure_config(void) {
	sys_once(&once_token, do_init_config, NULL, sys_once_flag_sigsafe);
};

ferr_t sys_config_read(sys_config_key_t key, void* buffer, size_t buffer_size, size_t* out_written_count) {
	ferr_t status = ferr_ok;
	size_t written_count = 0;

	ensure_config();

	switch (key) {
		case sys_config_key_minimum_stack_size: {
			if (buffer_size < sizeof(uint64_t)) {
				status = ferr_invalid_argument;
				goto out;
			}

			*(uint64_t*)buffer = constants.minimum_stack_size;
			written_count = sizeof(uint64_t);
		} break;

		case sys_config_key_page_size: {
			if (buffer_size < sizeof(uint64_t)) {
				status = ferr_invalid_argument;
				goto out;
			}

			*(uint64_t*)buffer = constants.page_size;
			written_count = sizeof(uint64_t);
		} break;

		case sys_config_key_total_thread_context_size: {
			if (buffer_size < sizeof(uint64_t)) {
				status = ferr_invalid_argument;
				goto out;
			}

			*(uint64_t*)buffer = constants.total_thread_context_size;
			written_count = sizeof(uint64_t);
		} break;

		case sys_config_key_minimum_thread_context_alignment_power: {
			if (buffer_size < sizeof(uint8_t)) {
				status = ferr_invalid_argument;
				goto out;
			}

			*(uint8_t*)buffer = constants.minimum_thread_context_alignment_power;
			written_count = sizeof(uint8_t);
		} break;

		default: {
			status = ferr_no_such_resource;
		} break;
	}

out:
	if (status == ferr_ok) {
		if (out_written_count) {
			*out_written_count = written_count;
		}
	}
	return status;
};

//
// convenience wrappers
//

uint64_t sys_config_read_minimum_stack_size(void) {
	uint64_t result;
	sys_abort_status(sys_config_read(sys_config_key_minimum_stack_size, &result, sizeof(result), NULL));
	return result;
};

uint64_t sys_config_read_page_size(void) {
	uint64_t result;
	sys_abort_status(sys_config_read(sys_config_key_page_size, &result, sizeof(result), NULL));
	return result;
};

uint64_t sys_config_read_total_thread_context_size(void) {
	uint64_t result;
	sys_abort_status(sys_config_read(sys_config_key_total_thread_context_size, &result, sizeof(result), NULL));
	return result;
};

uint8_t sys_config_read_minimum_thread_context_alignment_power(void) {
	uint8_t result;
	sys_abort_status(sys_config_read(sys_config_key_minimum_thread_context_alignment_power, &result, sizeof(result), NULL));
	return result;
};
