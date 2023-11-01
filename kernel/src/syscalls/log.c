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

#include <gen/ferro/userspace/syscall-handlers.h>
#include <ferro/core/console.h>
#include <ferro/userspace/uio.h>

ferr_t fsyscall_handler_log(const char* message, uint64_t message_length) {
	char* tmp = NULL;
	ferr_t status = ferr_ok;

	status = ferro_uio_copy_in((uintptr_t)message, message_length, (void**)&tmp);
	if (status != ferr_ok) {
		goto out;
	}

	fconsole_logn(tmp, message_length);

out:
	if (tmp) {
		ferro_uio_copy_free(tmp, message_length);
	}

	return status;
};
