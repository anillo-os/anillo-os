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

#include <libsys/ucs.h>
#include <libsys/abort.h>
#include <libsimple/libsimple.h>
#include <immintrin.h>

// DEBUG
#include <libsys/console.h>

extern void __sys_ucs_save(sys_ucs_context_t* context);
extern void __sys_ucs_switch(sys_ucs_context_t* out_old_context, const sys_ucs_context_t* new_context);

void sys_ucs_init_empty(sys_ucs_context_t* context) {
	simple_memset(context, 0, sizeof(*context));
	context->mxcsr = _mm_getcsr();
	__asm__ volatile("fnstcw %0" :: "m" (context->x87_cw));
};

void sys_ucs_init_current(sys_ucs_context_t* context, sys_ucs_init_current_flags_t flags) {
	__sys_ucs_save(context);
};

void sys_ucs_set_stack(sys_ucs_context_t* context, void* base, size_t size) {
	// subtract 8 so that the target function can push rbp and keep the stack aligned
	context->rsp = (uintptr_t)((char*)base + size) - 8;
};

void sys_ucs_set_entry(sys_ucs_context_t* context, sys_ucs_entry_f entry, void* entry_context) {
	context->rip = (uintptr_t)entry;
	context->rdi = (uintptr_t)entry_context;
};

void sys_ucs_switch(const sys_ucs_context_t* new_context, sys_ucs_context_t* out_old_context) {
	__sys_ucs_switch(out_old_context, new_context);
};
