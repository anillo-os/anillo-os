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

#ifndef _FERRO_SYSCALLS_CHANNELS_PRIVATE_H_
#define _FERRO_SYSCALLS_CHANNELS_PRIVATE_H_

#include <stdint.h>
#include <stddef.h>

#include <ferro/base.h>
#include <ferro/userspace/processes.h>
#include <ferro/core/channels.h>
#include <ferro/core/refcount.h>

FERRO_DECLARATIONS_BEGIN;

FERRO_STRUCT(fsyscall_channel_server_context) {
	frefcount_t refcount;
	fchannel_server_t* server;
	fchannel_realm_t* realm;
	size_t name_length;
	char name[];
};

extern const fproc_descriptor_class_t fsyscall_channel_descriptor_class;
extern const fproc_descriptor_class_t fsyscall_channel_server_context_descriptor_class;

FERRO_DECLARATIONS_END;

#endif // _FERRO_SYSCALLS_CHANNELS_PRIVATE_H_
