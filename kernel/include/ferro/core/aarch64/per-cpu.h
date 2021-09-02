/*
 * This file is part of Anillo OS
 * Copyright (C) 2021 Anillo OS Developers
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

/**
 * @file
 *
 * AARCH64 Per-CPU data subsystem.
 */

#ifndef _FERRO_CORE_AARCH64_PER_CPU_H_
#define _FERRO_CORE_AARCH64_PER_CPU_H_

#include <stdint.h>
#include <stddef.h>

#include <ferro/base.h>

FERRO_DECLARATIONS_BEGIN;

/**
 * @addtogroup Per-CPU
 *
 * The AARCH64 Per-CPU data subsystem.
 *
 * @{
 */

FERRO_STRUCT_FWD(fthread);
FERRO_STRUCT_FWD(farch_int_exception_frame);

FERRO_STRUCT(farch_per_cpu_data) {
	farch_per_cpu_data_t* base;
	uint64_t outstanding_interrupt_disable_count;
	fthread_t* current_thread;
	farch_int_exception_frame_t* current_exception_frame;
};

farch_per_cpu_data_t* farch_per_cpu_base_address(void);

#define FARCH_PER_CPU(_name) (farch_per_cpu_base_address()->_name)

/**
 * @}
 */

FERRO_DECLARATIONS_END;

#endif // _FERRO_CORE_AARCH64_PER_CPU_H_
