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

#ifndef _LIBSYS_MEMPOOL_PRIVATE_H_
#define _LIBSYS_MEMPOOL_PRIVATE_H_

#include <libsys/mempool.h>
#include <libsys/locks.h>
#include <libsimple/mempool.h>

extern sys_mutex_t mempool_global_lock;
extern simple_mempool_instance_t mempool_main_instance;

void sys_mempool_handoff(sys_mutex_t* lock, simple_mempool_instance_t* instance);

#endif // _LIBSYS_MEMPOOL_PRIVATE_H_
