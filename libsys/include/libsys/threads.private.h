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

#include <ferro/error.h>

LIBSYS_DECLARATIONS_BEGIN;

LIBSYS_STRUCT(sys_thread_object) {
	sys_object_t object;
	sys_thread_id_t id;
	void* tls[256];
};

LIBSYS_ENUM(uint64_t, sys_thread_tls_key) {
	sys_thread_tls_key_tls = 0,
	sys_thread_tls_key_self = 1,
};

#define LIBSYS_GS_RELATIVE __attribute__((address_space(256)))
#define LIBSYS_FS_RELATIVE __attribute__((address_space(257)))

LIBSYS_WUR ferr_t sys_thread_init(void);

LIBSYS_DECLARATIONS_END;

#endif // _LIBSYS_THREADS_PRIVATE_H_
