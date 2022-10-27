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
#include <libsimple/general.h>

extern void __sys_ucs_save(sys_ucs_context_t* context);
extern void __sys_ucs_switch(sys_ucs_context_t* out_old_context, const sys_ucs_context_t* new_context);

void sys_ucs_init_empty(sys_ucs_context_t* context) {
	simple_memset(context, 0, sizeof(*context));
};

void sys_ucs_init_current(sys_ucs_context_t* context, sys_ucs_init_current_flags_t flags) {
	__sys_ucs_save(context);
};

void sys_ucs_set_stack(sys_ucs_context_t* context, void* base, size_t size) {
	context->sp = (uintptr_t)base + size;
};

void sys_ucs_set_entry(sys_ucs_context_t* context, sys_ucs_entry_f entry, void* entry_context) {
	context->ip = (uintptr_t)entry;
	context->x0 = (uintptr_t)entry_context;
};

void sys_ucs_switch(const sys_ucs_context_t* new_context, sys_ucs_context_t* out_old_context) {
	__sys_ucs_switch(out_old_context, new_context);
};
