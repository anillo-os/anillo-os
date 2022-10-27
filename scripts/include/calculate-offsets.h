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

#ifndef _CALCULATE_OFFSETS_H_
#define _CALCULATE_OFFSETS_H_

#include <stddef.h>

#define OFFSET(_struct, _member) __asm__("# XXX " #_struct " XXX " #_member " = %c0" :: "i" (offsetof(struct _struct, _member)))
#define SIZE(_struct) __asm__("# XXX " #_struct " = %c0" :: "i" (sizeof(struct _struct)))

#define OFFSETS_BEGIN void _calculate_offsets() {

#define OFFSETS_END };

#endif // _CALCULATE_OFFSETS_H_
