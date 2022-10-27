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
 * x86_64 implementations of architecture-specific functions for the threads subsystem.
 */

#include <ferro/core/threads.private.h>
#include <ferro/core/paging.h>
#include <ferro/core/interrupts.h>
#include <libsimple/libsimple.h>
#include <ferro/core/mempool.h>
#include <ferro/core/panic.h>
#include <ferro/core/x86_64/xsave.h>

#include <stdatomic.h>

void farch_threads_runner(void);

void farch_thread_init_info(fthread_t* thread, fthread_initializer_f initializer, void* data) {
	farch_xsave_area_legacy_t* xsave_legacy;

	thread->saved_context->rip = (uintptr_t)farch_threads_runner;
	thread->saved_context->rsp = (uintptr_t)thread->stack_base + thread->stack_size;
	thread->saved_context->rdi = (uintptr_t)data;
	thread->saved_context->r10 = (uintptr_t)initializer;
	thread->saved_context->cs = farch_int_gdt_index_code * 8;
	thread->saved_context->ss = farch_int_gdt_index_data * 8;

	// set the reserved bit (bit 1) and the interrupt-enable bit (bit 9)
	thread->saved_context->rflags = (1ULL << 1) | (1ULL << 9);

	// initialize MXCSR
	xsave_legacy = (void*)thread->saved_context->xsave_area;
	xsave_legacy->mxcsr = 0x1f80ull | (0xffbfull << 32); // TODO: programmatically determine the xsave mask
};

fthread_t* fthread_current(void) {
	return FARCH_PER_CPU(current_thread);
};
