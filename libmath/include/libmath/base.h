/*
 * This file is part of Anillo OS
 * Copyright (C) 2024 Anillo OS Developers
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

#ifndef _LIBMATH_BASE_H_
#define _LIBMATH_BASE_H_

#include <libsimple/base.h>

#define LIBMATH_DECLARATIONS_BEGIN LIBSIMPLE_DECLARATIONS_BEGIN
#define LIBMATH_DECLARATIONS_END LIBSIMPLE_DECLARATIONS_END

#define LIBMATH_WUR LIBSIMPLE_WUR
#define LIBMATH_WUR_IGNORE LIBSIMPLE_WUR_IGNORE

#define LIBMATH_STRUCT LIBSIMPLE_STRUCT
#define LIBMATH_STRUCT_FWD LIBSIMPLE_STRUCT_FWD
#define LIBMATH_ENUM LIBSIMPLE_ENUM
#define LIBMATH_OPTIONS LIBSIMPLE_OPTIONS

#define LIBMATH_ALWAYS_INLINE LIBSIMPLE_ALWAYS_INLINE
#define LIBMATH_NO_RETURN LIBSIMPLE_NO_RETURN

#define math_assert(x) fassert(x)

#endif // _LIBMATH_BASE_H_
