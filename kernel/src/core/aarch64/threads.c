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
 * AARCH64 implementations of architecture-specific functions for the threads subsystem.
 */

#include <ferro/core/threads.private.h>
#include <ferro/core/per-cpu.private.h>

void farch_threads_runner(void);

void farch_thread_init_info(fthread_t* thread, fthread_initializer_f initializer, void* data) {
	thread->saved_context.pc = (uintptr_t)farch_threads_runner;
	thread->saved_context.sp = (uintptr_t)thread->stack_base + thread->stack_size;
	thread->saved_context.x0 = (uintptr_t)data;
	thread->saved_context.x19 = (uintptr_t)initializer;

	// leave the DAIF mask bits cleared to enable interrupts
	thread->saved_context.pstate = farch_thread_pstate_aarch64 | farch_thread_pstate_el1 | farch_thread_pstate_sp0;
};

fthread_t* fthread_current(void) {
	return FARCH_PER_CPU(current_thread);
};
