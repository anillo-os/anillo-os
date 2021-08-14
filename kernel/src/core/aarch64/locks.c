/**
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
//
// src/core/aarch64/locks.c
//
// AARCH64 lock implementations
//

#include <ferro/core/locks.h>
#include <ferro/core/interrupts.h>

void flock_spin_init(flock_spin_t* lock) {
	lock->flag = 0;
};

void flock_spin_lock(flock_spin_t* lock) {
	while (__atomic_test_and_set(&lock->flag, __ATOMIC_ACQUIRE)) {
		__asm__ volatile("yield" :::);
	}
};

bool flock_spin_try_lock(flock_spin_t* lock) {
	return !__atomic_test_and_set(&lock->flag, __ATOMIC_ACQUIRE);
};

void flock_spin_unlock(flock_spin_t* lock) {
	__atomic_clear(&lock->flag, __ATOMIC_RELEASE);
};

void flock_spin_intsafe_init(flock_spin_intsafe_t* lock) {
	flock_spin_init(&lock->base);
};

void flock_spin_intsafe_lock(flock_spin_intsafe_t* lock) {
	fint_disable();
	flock_spin_intsafe_lock_unsafe(lock);
};

void flock_spin_intsafe_lock_unsafe(flock_spin_intsafe_t* lock) {
	return flock_spin_lock(&lock->base);
};

bool flock_spin_intsafe_try_lock(flock_spin_intsafe_t* lock) {
	bool acquired;

	fint_disable();

	acquired = flock_spin_intsafe_try_lock_unsafe(lock);

	if (!acquired) {
		fint_enable();
	}

	return acquired;
};

bool flock_spin_intsafe_try_lock_unsafe(flock_spin_intsafe_t* lock) {
	return flock_spin_try_lock(&lock->base);
};

void flock_spin_intsafe_unlock(flock_spin_intsafe_t* lock) {
	flock_spin_intsafe_unlock_unsafe(lock);
	fint_enable();
};

void flock_spin_intsafe_unlock_unsafe(flock_spin_intsafe_t* lock) {
	return flock_spin_unlock(&lock->base);
};
