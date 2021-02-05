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

#ifndef _LIBK_LIBK_H_
#define _LIBK_LIBK_H_

#include <stddef.h>

/**
 * Copies `n` bytes from `source` to `destination`. `source` and `destination` MUST NOT overlap.
 *
 * @param destination Pointer to memory location to copy into.
 * @param source      Read-only pointer to memory location to copy from.
 * @param n           Number of bytes to copy from `source` to `destination`.
 *
 * @returns `destination`.
 *
 * @note Standard C function.
 */
void* memcpy(void* restrict destination, const void* restrict source, size_t n);

/**
 * Copies `n` bytes from `source` to `destination` `m` times. `source` and `destination` MUST NOT overlap.
 *
 * @param destination Pointer to memory location to copy into.
 * @param source      Read-only pointer to memory location to copy from.
 * @param n           Number of bytes to copy from `source` for each copy.
 * @param m           Number of copies to make of `source` in `destination`.
 *
 * @returns `destination`.
 *
 * @note Non-standard function.
 */
void* memclone(void* restrict destination, const void* restrict source, size_t n, size_t m);

/**
 * Returns the length of given string in bytes, as determined by the number of bytes before the null terminator.
 *
 * @param string The C string to measure.
 *
 * @returns The number of bytes in the string before the null terminator.
 *
 * @note Standard C function.
 */
size_t strlen(const char* string);

/**
 * Copies `n` bytes from `source` to `destination`. `source` and `destination` are allowed to overlap.
 *
 * @param destination Pointer to memory location to copy into.
 * @param source      Read-only pointer to memory location to copy from.
 * @param n           Number of bytes to copy from `source` to `destination`.
 *
 * @returns `destination`.
 *
 * @note Standard C function.
 */
void* memmove(void* destination, const void* source, size_t n);

#endif // _LIBK_LIBK_H_
