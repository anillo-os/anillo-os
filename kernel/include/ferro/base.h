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

#define FERRO_ENUM(type, name) \
	typedef type name ## _t; \
	enum name

#define FERRO_STRUCT(name) \
	typedef struct name name ## _t; \
	struct name

#define FERRO_STRUCT_FWD(name) \
	typedef struct name name ## _t;

#define FERRO_OPTIONS(type, name) \
	typedef type name ## _t; \
	enum __attribute__((flag_enum)) name

#define FERRO_WUR __attribute__((warn_unused_result))

#define FERRO_PRINTF(a, b) __attribute__((format(printf, a, b)))

#define FERRO_NAKED __attribute__((naked))

#define FERRO_USED __attribute__((used))

// just a prettier name for `_Static_assert`
#define FERRO_VERIFY(expr, message) _Static_assert(expr, message)

#define FERRO_VERIFY_OFFSET(type, member, offset) FERRO_VERIFY(offsetof(type, member) == offset, "Offset verification failed for " #member " in " #type " (expected offset to equal " #offset ")")

// note: this is the actual alignment of the type, not the required alignment (like what `alignof` returns)
#define FERRO_VERIFY_ALIGNMENT(type, alignment) FERRO_VERIFY((sizeof(type) & ((alignment) - 1)) == 0, "Alignment verification failed for " #type " (expected type to be aligned to " #alignment " bytes)");

#define FSTRINGIFY_HELPER(x) #x
#define FSTRINGIFY(x) FSTRINGIFY_HELPER(x)

FERRO_ALWAYS_INLINE void fassert_helper(int result, const char* expr) {
	if (!result) {
		__builtin_unreachable();
	}
};

#define fassert(x) fassert_helper(!!(x), #x)

#endif // _FERRO_BASE_H_
