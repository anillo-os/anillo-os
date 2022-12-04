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

#include <libsys/threads.private.h>
#include <gen/libsyscall/syscall-wrappers.h>
#include <libsys/abort.h>

void __sys_thread_setup(sys_thread_object_t* thread) {
	sys_abort_status(libsyscall_wrapper_thread_set_fs(&thread->tls[0]));
	__sys_thread_setup_common();
};

extern bool __sys_thread_init_complete;

sys_thread_t* sys_thread_current(void) {
	if (!__sys_thread_init_complete) {
		return NULL;
	}
	return *(void* LIBSYS_FS_RELATIVE*)(sys_thread_tls_key_self * sizeof(void*));
};
