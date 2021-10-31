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

#include <libsys/libsys.private.h>
#include <libelf/libelf.h>

#define dylink_abort_status(_expression) ({ \
		if ((_expression) != ferr_ok) { \
			sys_console_log_f("Expression returned non-OK status: (%s:%d)" #_expression, __FILE__, __LINE__); \
			sys_abort(); \
		} \
		(void)0; \
	})

void _start(void) {
	sys_file_t* binary_file = NULL;
	elf_header_t header = {0};
	size_t read = 0;

	sys_abort_status(sys_console_init());

	sys_console_log("Hello from dylink!\n");

	dylink_abort_status(sys_file_open_special(sys_file_special_id_process_binary, &binary_file));

	sys_console_log_f("Successfully opened file for process binary! Address is %p\n", binary_file);

	dylink_abort_status(sys_file_read(binary_file, 0, sizeof(elf_header_t), &header, &read));

	if (read != sizeof(elf_header_t)) {
		sys_console_log_f("Didn't read full header (read=%zu; needed=%zu)\n", read, sizeof(elf_header_t));
		sys_abort();
	}

	sys_console_log("Read ELF header successfully!\n");
	sys_console_log_f("ELF type = %d\n", header.type);

	sys_release(binary_file);

	sys_exit(0);
};
