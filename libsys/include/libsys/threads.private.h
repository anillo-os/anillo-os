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

#ifndef _LIBSYS_THREADS_PRIVATE_H_
#define _LIBSYS_THREADS_PRIVATE_H_

#include <libsys/base.h>
#include <libsys/threads.h>
#include <libsys/objects.private.h>
#include <libsys/locks.h>

#include <libsys/ghmap.h>

#include <ferro/error.h>

#include <gen/libsyscall/syscall-wrappers.h>

LIBSYS_DECLARATIONS_BEGIN;

LIBSYS_STRUCT(sys_thread_signal_handler) {
	sys_thread_signal_handler_f handler;
	void* context;
};

LIBSYS_STRUCT(sys_thread_object) {
	sys_object_t object;
	sys_thread_id_t id;
	sys_event_t death_event;
	void* free_on_death;
	void* tls[256];
	simple_ghmap_t external_tls;
	uint8_t block_signals;
	uint64_t signal_block_count;
	simple_ghmap_t signal_handlers;
	sys_thread_special_signal_mapping_t special_signal_mapping;
};

LIBSYS_ENUM(uint64_t, sys_thread_tls_key) {
	sys_thread_tls_key_tls = 0,
	sys_thread_tls_key_self = 1,
};

#define LIBSYS_GS_RELATIVE __attribute__((address_space(256)))
#define LIBSYS_FS_RELATIVE __attribute__((address_space(257)))

LIBSYS_STRUCT(sys_thread_signal_info_private) {
	sys_thread_signal_info_t public;
	libsyscall_signal_info_t* original;
};

LIBSYS_WUR ferr_t sys_thread_init(void);

void __sys_thread_setup_common(void);

LIBSYS_DECLARATIONS_END;

#endif // _LIBSYS_THREADS_PRIVATE_H_
