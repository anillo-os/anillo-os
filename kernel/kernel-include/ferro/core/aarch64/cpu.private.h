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

#ifndef _FERRO_CORE_AARCH64_CPU_PRIVATE_H_
#define _FERRO_CORE_AARCH64_CPU_PRIVATE_H_

#include <ferro/core/cpu.private.h>
#include <ferro/core/aarch64/per-cpu.private.h>

FERRO_DECLARATIONS_BEGIN;

FERRO_STRUCT(fcpu) {
	farch_per_cpu_data_t* per_cpu_data;
};

FERRO_ALWAYS_INLINE void fcpu_do_work(void) {
	for (fcpu_interrupt_work_item_t* work_item = fcpu_interrupt_work_queue_next(&fcpu_broadcast_queue, FARCH_PER_CPU(last_ipi_work_id)); work_item != NULL; work_item = fcpu_interrupt_work_queue_next(&fcpu_broadcast_queue, FARCH_PER_CPU(last_ipi_work_id))) {
		FARCH_PER_CPU(last_ipi_work_id) = work_item->work_id;
		work_item->work(work_item->context);
		fcpu_interrupt_work_item_checkout(work_item);
	}
};

FERRO_DECLARATIONS_END;

#endif // _FERRO_CORE_AARCH64_CPU_PRIVATE_H_
