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

#include <libsys/locks.h>
#include <ferro/platform.h>

void sys_spinlock_init(sys_spinlock_t* spinlock) {
	spinlock->internal = 0;
};

void sys_spinlock_lock(sys_spinlock_t* spinlock) {
	while (__atomic_exchange_n(&spinlock->internal, 1, __ATOMIC_ACQUIRE) == 1) {
#if FERRO_ARCH == FERRO_ARCH_x86_64
		__asm__ volatile("pause" :::);
#elif FERRO_ARCH == FERRO_ARCH_aarch64
		__asm__ volatile("yield" :::);
#endif
	}
};

void sys_spinlock_unlock(sys_spinlock_t* spinlock) {
	__atomic_store_n(&spinlock->internal, 0, __ATOMIC_RELEASE);
};

bool sys_spinlock_try_lock(sys_spinlock_t* spinlock) {
	uint8_t expected = 0;
	return __atomic_compare_exchange_n(&spinlock->internal, &expected, 1, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
};
