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

#include <gen/ferro/userspace/syscall-handlers.h>
#include <ferro/core/paging.h>
#include <ferro/core/per-cpu.private.h>

ferr_t fsyscall_handler_constants(ferro_constants_t* out_constants) {
	out_constants->page_size = FPAGE_PAGE_SIZE;
	out_constants->minimum_stack_size = 4 * FPAGE_PAGE_SIZE;

#if FERRO_ARCH == FERRO_ARCH_x86_64
	out_constants->minimum_thread_context_alignment_power = fpage_round_up_to_alignment_power(64);
	// pad the thread context size so that the xsave area can be aligned to 64 bytes
	out_constants->total_thread_context_size = fpage_align_address_up(sizeof(ferro_thread_context_t), fpage_round_up_to_alignment_power(64)) + FARCH_PER_CPU(xsave_area_size);
	out_constants->xsave_area_size = FARCH_PER_CPU(xsave_area_size);
#elif FERRO_ARCH == FERRO_ARCH_aarch64
	out_constants->minimum_thread_context_alignment_power = fpage_round_up_to_alignment_power(16);
	// pad the thread context size so that the FP register area can be aligned to 16 bytes
	out_constants->total_thread_context_size = fpage_align_address_up(sizeof(ferro_thread_context_t), fpage_round_up_to_alignment_power(16)) + (sizeof(__uint128_t) * 32);
#endif

	return ferr_ok;
};
