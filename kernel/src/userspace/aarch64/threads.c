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

#include <ferro/userspace/threads.private.h>
#include <ferro/core/interrupts.h>
#include <libsimple/libsimple.h>
#include <ferro/core/panic.h>

void futhread_jump_user_self_arch(fthread_t* uthread, futhread_data_t* udata, void* address) {
	fpanic("TODO");
};

void futhread_ending_interrupt_arch(fthread_t* uthread, futhread_data_t* udata) {
	fpanic("TODO");
};

void futhread_arch_init(void) {
	// TODO
};
