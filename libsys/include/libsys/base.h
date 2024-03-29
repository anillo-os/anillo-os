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

#ifndef _LIBSYS_BASE_H_
#define _LIBSYS_BASE_H_

#include <ferro/base.h>

#define LIBSYS_NO_RETURN FERRO_NO_RETURN
#define LIBSYS_DECLARATIONS_BEGIN FERRO_DECLARATIONS_BEGIN
#define LIBSYS_DECLARATIONS_END FERRO_DECLARATIONS_END
#define LIBSYS_STRUCT FERRO_STRUCT
#define LIBSYS_PACKED_STRUCT FERRO_PACKED_STRUCT
#define LIBSYS_ALWAYS_INLINE FERRO_ALWAYS_INLINE
#define LIBSYS_ENUM FERRO_ENUM
#define LIBSYS_STRUCT_FWD FERRO_STRUCT_FWD
#define LIBSYS_WUR FERRO_WUR
#define LIBSYS_PRINTF FERRO_PRINTF
#define LIBSYS_OPTIONS FERRO_OPTIONS
#define LIBSYS_WUR_IGNORE FERRO_WUR_IGNORE
#define LIBSYS_UNION FERRO_UNION
#define LIBSYS_TYPED_FUNC FERRO_TYPED_FUNC

#endif // _LIBSYS_BASE_H_
