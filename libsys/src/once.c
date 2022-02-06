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

#include <libsys/once.h>
#include <stdbool.h>
#include <gen/libsyscall/syscall-wrappers.h>

// based on https://github.com/bugaevc/lets-write-sync-primitives

LIBSYS_ENUM(uint64_t, sys_once_state) {
	sys_once_state_init = 0,
	sys_once_state_done = 1,
	sys_once_state_perform_no_wait = 2,
	sys_once_state_perform_wait = 3,
};

void sys_once(sys_once_t* token, sys_once_f initializer, void* context) {
	uint64_t old_state = sys_once_state_init;

	if (__atomic_compare_exchange_n(token, &old_state, sys_once_state_perform_no_wait, false, __ATOMIC_ACQUIRE, __ATOMIC_ACQUIRE)) {
		// we saw "init" and changed it to "perform_no_wait";
		// we're now the ones that need to call the initializer

		initializer(context);

		// now that we're done, we need to check whether anyone was waiting and, if so, wake them up
		old_state = __atomic_exchange_n(token, sys_once_state_done, __ATOMIC_RELEASE);

		if (old_state == sys_once_state_perform_wait) {
			// wake up everyone who was waiting for us to finish
			libsyscall_wrapper_futex_wake(token, 0, UINT64_MAX, 0);
		}

		// we're done now
		return;
	}

	// otherwise, we did not see "init", so let's figure out what to do

	// once we see "done", we know we're done and can return
	while (old_state != sys_once_state_done) {
		if (old_state == sys_once_state_perform_no_wait) {
			// we're the first waiter; let's update the state to let the performer know
			if (!__atomic_compare_exchange_n(token, &old_state, sys_once_state_perform_wait, false, __ATOMIC_ACQUIRE, __ATOMIC_ACQUIRE)) {
				// if we failed to exchange it, the performer might've already finished;
				// let's loop back around and check
				continue;
			}

			// if we successfully updated the state to "perform_wait", let's continue on and wait
		}

		// `old_state` should only be "perform_wait" here

		// let's wait
		libsyscall_wrapper_futex_wait(token, 0, sys_once_state_perform_wait, 0, 0, 0);

		// we've been woken up, but it might be a spurious wakeup;
		// let's loop back around and check to make sure we're actually done
		old_state = __atomic_load_n(token, __ATOMIC_ACQUIRE);
	}

	// if we got here, we saw "done" and we can now return
};
