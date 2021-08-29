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
//
// src/core/aarch64/thread-runner.s
//
// AARCH64 thread entry function
//

.text
.global farch_threads_runner
farch_threads_runner:
	// x0 holds the user data for the initializer
	// x19 holds the initializer

	// x0 already holds the user data
	blr x19

	// time to die
	b fthread_kill_self
	udf #0 // this function should never return
