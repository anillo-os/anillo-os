/*
 * This file is part of Anillo OS
 * Copyright (C) 2023 Anillo OS Developers
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

#ifndef _LIBJSON_BASE_H_
#define _LIBJSON_BASE_H_

#include <libsys/libsys.h>

#define LIBJSON_NO_RETURN LIBSYS_NO_RETURN
#define LIBJSON_DECLARATIONS_BEGIN LIBSYS_DECLARATIONS_BEGIN
#define LIBJSON_DECLARATIONS_END LIBSYS_DECLARATIONS_END
#define LIBJSON_STRUCT LIBSYS_STRUCT
#define LIBJSON_PACKED_STRUCT LIBSYS_PACKED_STRUCT
#define LIBJSON_ALWAYS_INLINE LIBSYS_ALWAYS_INLINE
#define LIBJSON_ENUM LIBSYS_ENUM
#define LIBJSON_STRUCT_FWD LIBSYS_STRUCT_FWD
#define LIBJSON_WUR LIBSYS_WUR
#define LIBJSON_PRINTF LIBSYS_PRINTF
#define LIBJSON_OPTIONS LIBSYS_OPTIONS
#define LIBJSON_WUR_IGNORE LIBSYS_WUR_IGNORE
#define LIBJSON_TYPED_FUNC LIBSYS_TYPED_FUNC

#define json_assert(x) fassert(x)

#endif // _LIBJSON_BASE_H_
