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

#ifndef _FERRO_CORE_CPU_PRIVATE_H_
#define _FERRO_CORE_CPU_PRIVATE_H_

#include <ferro/core/cpu.h>
#include <ferro/core/locks.spin.h>
#include <ferro/platform.h>

FERRO_DECLARATIONS_BEGIN;

FERRO_OPTIONS(uint64_t, fcpu_interrupt_work_item_flags) {
	fcpu_interrupt_work_item_flag_free_on_finish = 1 << 0,
	fcpu_interrupt_work_item_flag_exclude_origin = 1 << 1,
	fcpu_interrupt_work_item_flag_completed      = 1 << 2,
};

FERRO_ENUM(uint64_t, fcpu_interrupt_work_id) {
	fcpu_interrupt_work_id_invalid = 0,
};

FERRO_STRUCT_FWD(fcpu_interrupt_work_queue);

FERRO_STRUCT(fcpu_interrupt_work_item) {
	fcpu_interrupt_work_item_t** prev;
	fcpu_interrupt_work_item_t* next;
	fcpu_interrupt_work_queue_t* queue;
	fcpu_interrupt_work_item_flags_t flags;
	fcpu_id_t origin;
	fcpu_interrupt_work_f work;
	void* context;
	uint64_t expected_count;
	uint64_t checkin_count;
	uint64_t checkout_count;
	fcpu_interrupt_work_id_t work_id;
};

FERRO_STRUCT(fcpu_interrupt_work_queue) {
	flock_spin_intsafe_t lock;
	fcpu_interrupt_work_item_t* head;
	fcpu_interrupt_work_item_t* tail;
};

extern fcpu_interrupt_work_queue_t fcpu_broadcast_queue;

fcpu_interrupt_work_id_t fcpu_interrupt_work_next_id(void);
fcpu_interrupt_work_item_t* fcpu_interrupt_work_queue_next(fcpu_interrupt_work_queue_t* work_queue, fcpu_interrupt_work_id_t last_id);
void fcpu_interrupt_work_queue_add(fcpu_interrupt_work_queue_t* work_queue, fcpu_interrupt_work_item_t* work_item);
void fcpu_interrupt_work_item_checkout(fcpu_interrupt_work_item_t* work_item);

// arch-dependent functions

FERRO_WUR ferr_t fcpu_arch_interrupt_all(bool include_current);

FERRO_DECLARATIONS_END;

// include the arch-dependent header
#if FERRO_ARCH == FERRO_ARCH_x86_64
	#include <ferro/core/x86_64/cpu.private.h>
#elif FERRO_ARCH == FERRO_ARCH_aarch64
	#include <ferro/core/aarch64/cpu.private.h>
#else
	#error Unrecognized/unsupported CPU architecture! (see <ferro/core/cpu.private.h>)
#endif

#endif // _FERRO_CORE_CPU_PRIVATE_H_
