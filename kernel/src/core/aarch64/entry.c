/**
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
//
// entry.c
//
// kernel entry point
//

#include <stdbool.h>
#include <stddef.h>
#include <ferro/core/entry.h>
#include <ferro/core/framebuffer.h>
#include <ferro/core/console.h>
#include <ferro/core/paging.h>

#define UART0DR ((volatile unsigned int*)0x101f1000)

FERRO_INLINE void hang_forever() {
	while (true) {
		__asm__(
			"msr daifset, #3\n"
			"hlt 0\n"
		);
	}
};

FERRO_INLINE void uart_print(const char* string) {
	while (string[0] != '\0') {
		*UART0DR = (unsigned int)string[0];
		++string;
	}
};

//__attribute__((section(".text.ferro_entry")))
void ferro_entry(void* initial_pool, size_t initial_pool_page_count, ferro_boot_data_info_t* boot_data, size_t boot_data_count) {
	uart_print("Hello, world!");
	hang_forever();
};
