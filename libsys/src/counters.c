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

#include <libsys/counters.private.h>
#include <gen/libsyscall/syscall-wrappers.h>
#include <libsys/timeout.private.h>

#include <libsys/abort.h>

static void sys_counter_wake(sys_counter_object_t* counter, sys_counter_value_t prev_value) {
	if (prev_value & sys_counter_flag_need_to_wake) {
		libsyscall_wrapper_futex_wake(&counter->value, 0, UINT64_MAX, 0);
	}
};

static void sys_counter_destroy(sys_object_t* obj) {
	sys_counter_object_t* counter = (void*)obj;

	// wake up anyone waiting on the counter
	// (really should be an error, but do it just in case to avoid having permanently suspended threads)
	sys_counter_wake(counter, __atomic_load_n(&counter->value, __ATOMIC_ACQUIRE));

	sys_object_destroy(obj);
};

static const sys_object_class_t counter_class = {
	LIBSYS_OBJECT_CLASS_INTERFACE(NULL),
	.destroy = sys_counter_destroy,
};

const sys_object_class_t* sys_object_class_counter(void) {
	return &counter_class;
};

ferr_t sys_counter_create(sys_counter_value_t initial_value, sys_counter_t** out_counter) {
	ferr_t status = ferr_ok;
	sys_counter_object_t* counter = NULL;

	status = sys_object_new(&counter_class, sizeof(*counter) - sizeof(counter->object), (void*)&counter);
	if (status != ferr_ok) {
		goto out;
	}

	counter->value = 0;

out:
	if (status == ferr_ok) {
		*out_counter = (void*)counter;
	} else {
		if (counter) {
			sys_release((void*)counter);
		}
	}
	return status;
};

sys_counter_value_t sys_counter_value(sys_counter_t* obj) {
	sys_counter_object_t* counter = (void*)obj;
	return __atomic_load_n(&counter->value, __ATOMIC_ACQUIRE) & ~sys_counter_flag_need_to_wake;
};

void sys_counter_increment(sys_counter_t* obj) {
	sys_counter_object_t* counter = (void*)obj;
	// relaxed load is okay; `sys_counter_set` will perform the necessary synchronization for us
	sys_counter_value_t prev = __atomic_load_n(&counter->value, __ATOMIC_RELAXED);
	sys_counter_set(obj, prev + 1);
};

void sys_counter_set(sys_counter_t* obj, sys_counter_value_t value) {
	sys_counter_object_t* counter = (void*)obj;
	value &= ~sys_counter_flag_need_to_wake;
	sys_counter_value_t prev = __atomic_load_n(&counter->value, __ATOMIC_RELAXED);
	// use acq_rel because we want to see everything that happened before the previous counter value change
	// and we want everyone after us to see everything we did before this counter value change
	while (!__atomic_compare_exchange_n(&counter->value, &prev, value, true, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED));
	// wake if necessary
	sys_counter_wake(counter, prev);
};

void sys_counter_wait(sys_counter_t* obj, uint64_t timeout, sys_timeout_type_t timeout_type) {
	sys_counter_object_t* counter = (void*)obj;
	sys_counter_value_t val = __atomic_or_fetch(&counter->value, sys_counter_flag_need_to_wake, __ATOMIC_ACQUIRE);
	libsyscall_wrapper_futex_wait(&counter->value, 0, val, timeout, sys_timeout_type_to_libsyscall_timeout_type(timeout_type), 0);
};

void sys_counter_wait_value(sys_counter_t* obj, sys_counter_value_t target_value, uint64_t timeout, sys_timeout_type_t timeout_type) {
	sys_counter_object_t* counter = (void*)obj;
	// TODO
	// we need a way of obtaining the current time so we can modify the timeout accordingly on spurious wakeups
	sys_abort();
};
