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

#ifndef _FERRO_USERSPACE_FUTEX_H_
#define _FERRO_USERSPACE_FUTEX_H_

#include <stdint.h>

#include <ferro/base.h>
#include <ferro/core/refcount.h>
#include <ferro/core/waitq.h>
#include <ferro/core/ghmap.h>
#include <ferro/core/locks.h>

FERRO_DECLARATIONS_BEGIN;

FERRO_STRUCT_FWD(futex_table);

FERRO_STRUCT(futex) {
	futex_table_t* table;
	uintptr_t address;
	uint64_t channel;
	frefcount_t reference_count;
	fwaitq_t waitq;
};

FERRO_STRUCT(futex_table) {
	simple_ghmap_t table;
	flock_mutex_t mutex;
};

FERRO_WUR ferr_t futex_table_init(futex_table_t* table);
void futex_table_destroy(futex_table_t* table);

FERRO_WUR ferr_t futex_lookup(futex_table_t* table, uintptr_t address, uint64_t channel, futex_t** out_futex);
void futex_release(futex_t* futex);

FERRO_DECLARATIONS_END;

#endif // _FERRO_USERSPACE_FUTEX_H_
