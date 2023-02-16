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

#ifndef _LIBSIMPLE_BASE_H_
#define _LIBSIMPLE_BASE_H_

#include <ferro/base.h>

#define LIBSIMPLE_DECLARATIONS_BEGIN FERRO_DECLARATIONS_BEGIN
#define LIBSIMPLE_DECLARATIONS_END   FERRO_DECLARATIONS_END

#define LIBSIMPLE_WUR FERRO_WUR

#define LIBSIMPLE_STRUCT FERRO_STRUCT
#define LIBSIMPLE_STRUCT_FWD FERRO_STRUCT_FWD
#define LIBSIMPLE_ENUM FERRO_ENUM
#define LIBSIMPLE_OPTIONS FERRO_OPTIONS

#define LIBSIMPLE_ALIAS(orig_name) __attribute__((alias(#orig_name)))

#define LIBSIMPLE_ALWAYS_INLINE FERRO_ALWAYS_INLINE
#define LIBSIMPLE_NO_RETURN FERRO_NO_RETURN

#endif // _LIBSIMPLE_BASE_H_
