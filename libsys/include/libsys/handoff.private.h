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

#ifndef _LIBSYS_HANDOFF_PRIVATE_H_
#define _LIBSYS_HANDOFF_PRIVATE_H_

#include <libsys/base.h>
#include <libsys/console.private.h>

#include <ferro/error.h>

LIBSYS_DECLARATIONS_BEGIN;

LIBSYS_STRUCT(sys_handoff_context) {
	sys_stream_handle_t console_stream_handle;
};

/**
 * Prepares a handoff context by moving data into the context and cleaning up any necessary data in the current libsys instance.
 */
ferr_t sys_handoff_source(sys_handoff_context_t* context);

/**
 * Uses a handoff context by moving data out of the context and initializing any necessary data in the current libsystem instance.
 */
ferr_t sys_handoff_destination(sys_handoff_context_t* context);

LIBSYS_DECLARATIONS_END;

#endif // _LIBSYS_HANDOFF_PRIVATE_H_
