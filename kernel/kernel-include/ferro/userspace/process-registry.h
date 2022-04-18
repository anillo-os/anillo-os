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

#ifndef _FERRO_USERSPACE_PROCESS_REGISTRY_H_
#define _FERRO_USERSPACE_PROCESS_REGISTRY_H_

#include <ferro/base.h>
#include <ferro/userspace/processes.h>

FERRO_DECLARATIONS_BEGIN;

void fprocreg_init(void);

ferr_t fprocreg_register(fproc_t* process);
ferr_t fprocreg_unregister(fproc_id_t id);

ferr_t fprocreg_lookup(fproc_id_t id, bool retain, fproc_t** out_process);

FERRO_DECLARATIONS_END;

#endif // _FERRO_USERSPACE_PROCESS_REGISTRY_H_
