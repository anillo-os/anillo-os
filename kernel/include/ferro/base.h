/**
 * This file is part of Anillo OS
 * Copyright (C) 2020 Anillo OS Developers
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

#ifndef _FERRO_BASE_H_
#define _FERRO_BASE_H_

#ifdef __cplusplus
	#define FERRO_DECLARATIONS_BEGIN extern "C" {
#else
	#define FERRO_DECLARATIONS_BEGIN
#endif

#ifdef __cplusplus
	#define FERRO_DECLARATIONS_END }
#else
	#define FERRO_DECLARATIONS_END
#endif

#define FERRO_ALWAYS_INLINE static __attribute__((always_inline))

#define FERRO_NO_RETURN __attribute__((noreturn))

#define FERRO_PACKED_STRUCT(name) \
	typedef struct __attribute__((packed)) name name ## _t; \
	struct __attribute__((packed)) name

#define FERRO_ENUM(type, name) typedef type name ## _t; enum name

#define FERRO_STRUCT(name) \
	typedef struct name name ## _t; \
	struct name

#define FERRO_OPTIONS(type, name) \
	typedef type name ## _t; \
	enum __attribute__((flag_enum)) name

#endif // _FERRO_BASE_H_
