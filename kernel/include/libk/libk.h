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

/**
 * @file
 *
 * Minimal libc-like functions for kernel-space.
 */

#ifndef _LIBK_LIBK_H_
#define _LIBK_LIBK_H_

#include <stddef.h>

/**
 * Copies @p n bytes from @p source to @p destination. @p source and @p destination MUST NOT overlap.
 *
 * @param destination Pointer to memory location to copy into.
 * @param source      Read-only pointer to memory location to copy from.
 * @param n           Number of bytes to copy from @p source to @p destination.
 *
 * @returns @p destination.
 *
 * @note Standard C function.
 */
void* memcpy(void* restrict destination, const void* restrict source, size_t n);

/**
 * Copies @p n bytes from @p source to @p destination @p m times. @p source and @p destination MUST NOT overlap.
 *
 * @param destination Pointer to memory location to copy into.
 * @param source      Read-only pointer to memory location to copy from.
 * @param n           Number of bytes to copy from @p source for each copy.
 * @param m           Number of copies to make of @p source in @p destination.
 *
 * @returns @p destination.
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
 * Copies @p n bytes from @p source to @p destination. @p source and @p destination are allowed to overlap.
 *
 * @param destination Pointer to memory location to copy into.
 * @param source      Read-only pointer to memory location to copy from.
 * @param n           Number of bytes to copy from @p source to @p destination.
 *
 * @returns @p destination.
 *
 * @note Standard C function.
 */
void* memmove(void* destination, const void* source, size_t n);

/**
 * Sets @p n bytes in @p destination to @p value.
 *
 * @param destination Pointer to memory location to start assigning to.
 * @param value       Value to assign to each byte. This parameter is an `int` (for historical reasons), but it is interpreted as an `unsigned char` when used.
 * @param n           Number of bytes to assign to starting from @p destination.
 *
 * @returns @p destination.
 *
 * @note Standard C function.
 */
void* memset(void* destination, int value, size_t n);

/**
 * Compares at most @p n bytes from both strings and returns an indication of the lexicographal order of the two strings.
 *
 * The function will stop whenever it encounters a null terminator ('\0') in either string, but it will also stop if it reaches the maximum number of characters that can be read.
 *
 * @param first  The first string to compare.
 * @param second The second string to compare.
 * @param n      The maximum number of bytes to read from each.
 *
 * @retval -1 @p first is sorted before @p second.
 * @retval  1 @p second is sorted before @p first.
 * @retval  0 Both are sorted equally.
 *
 * @note Standard C function.
 */
int strncmp(const char* first, const char* second, size_t n);

/**
 * Compares @p n bytes from both arguments and returns an indication of which contains the first lower value.
 *
 * Unlike strncmp(), this function will NOT stop when it encounters a null terminator. It will ALWAYS compare @p n bytes.
 *
 * @param first  The first memory region to compare.
 * @param second The second memory region to compare.
 * @param n      The number of bytes to read from each.
 *
 * @retval -1 @p first has the first lower value.
 * @retval  1 @p second has the first lower value.
 * @retval  0 They contain the exact same values.
 *
 * @note Standard C function.
 */
int memcmp(const void* first, const void* second, size_t n);

#endif // _LIBK_LIBK_H_
