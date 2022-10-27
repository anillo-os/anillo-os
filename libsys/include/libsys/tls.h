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

#ifndef _LIBSYS_TLS_H_
#define _LIBSYS_TLS_H_

#include <stdint.h>

#include <libsys/base.h>

#include <ferro/error.h>

LIBSYS_DECLARATIONS_BEGIN;

typedef uint64_t sys_tls_key_t;
typedef uintptr_t sys_tls_value_t;

typedef void (*sys_tls_destructor_f)(sys_tls_value_t value);

LIBSYS_WUR ferr_t sys_tls_key_create(sys_tls_destructor_f destructor, sys_tls_key_t* out_key);
LIBSYS_WUR ferr_t sys_tls_get(sys_tls_key_t key, sys_tls_value_t* out_value);
LIBSYS_WUR ferr_t sys_tls_set(sys_tls_key_t key, sys_tls_value_t value);
LIBSYS_WUR ferr_t sys_tls_unset(sys_tls_key_t key);

LIBSYS_DECLARATIONS_END;

#endif // _LIBSYS_TLS_H_
