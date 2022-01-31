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

ferr_t sys_config_read(sys_config_key_t key, void* buffer, size_t buffer_size, size_t* out_written_count) {
	ferr_t status = ferr_ok;
	size_t written_count = 0;

	switch (key) {
		case sys_config_key_minimum_stack_size: {
			if (buffer_size < sizeof(uint64_t)) {
				status = ferr_invalid_argument;
				goto out;
			}

			*(uint64_t*)buffer = 4096ULL * 4;
			written_count = sizeof(uint64_t);
		} break;

		case sys_config_key_page_size: {
			if (buffer_size < sizeof(uint64_t)) {
				status = ferr_invalid_argument;
				goto out;
			}

			// TODO: determine this somehow
			*(uint64_t*)buffer = 4096ULL;
			written_count = sizeof(uint64_t);
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
