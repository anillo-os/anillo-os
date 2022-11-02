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
 * AARCH64 implementations of architecture-specific components for threads subsystem; after-header.
 */

#ifndef _FERRO_CORE_AARCH64_THREADS_AFTER_H_
#define _FERRO_CORE_AARCH64_THREADS_AFTER_H_

#include <stdint.h>

#include <ferro/base.h>
#include <ferro/core/threads.h>

FERRO_DECLARATIONS_BEGIN;

FERRO_ALWAYS_INLINE bool fthread_saved_context_is_kernel_space(fthread_saved_context_t* saved_context) {
	return (saved_context->pstate & farch_thread_pstate_el1) != 0;
};

FERRO_DECLARATIONS_END;

#endif // _FERRO_CORE_AARCH64_THREADS_AFTER_H_
