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

#ifndef _LIBSYS_UCS_H_
#define _LIBSYS_UCS_H_

#include <stdint.h>
#include <stddef.h>

#include <libsys/base.h>
#include <ferro/platform.h>

#if FERRO_ARCH == FERRO_ARCH_x86_64
	#include <libsys/x86_64/ucs.h>
#elif FERRO_ARCH == FERRO_ARCH_aarch64
	#include <libsys/aarch64/ucs.h>
#else
	#error Unknown architecture
#endif

LIBSYS_DECLARATIONS_BEGIN;

LIBSYS_OPTIONS(uint64_t, sys_ucs_init_current_flags) {
	sys_ucs_init_current_flag_reserved = 1 << 0,
};

typedef void (*sys_ucs_entry_f)(void* context);

void sys_ucs_init_empty(sys_ucs_context_t* context);
void sys_ucs_init_current(sys_ucs_context_t* context, sys_ucs_init_current_flags_t flags);

void sys_ucs_set_stack(sys_ucs_context_t* context, void* base, size_t size);
void sys_ucs_set_entry(sys_ucs_context_t* context, sys_ucs_entry_f entry, void* entry_context);

void sys_ucs_switch(const sys_ucs_context_t* new_context, sys_ucs_context_t* out_old_context);

LIBSYS_DECLARATIONS_END;

#endif // _LIBSYS_UCS_H_
