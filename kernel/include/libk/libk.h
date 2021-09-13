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
#include <stdint.h>
#include <stdbool.h>

#include <ferro/error.h>

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
 * Returns the length of given string in bytes, as determined by the number of bytes before the null terminator.
 *
 * @param string     The C string to measure.
 * @param max_length The maximum number of characters in @p string to look through.
 *
 * @returns The number of bytes in the string before the null terminator, or @p max_length if none was found before processing the given number of characters.
 *
 * @note Non-standard function.
 */
size_t strnlen(const char* string, size_t max_length);

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

/**
 * Determines whether @p character is a whitespace character.
 *
 * @param character The character to check.
 *
 * @retval 0 @p character is not a whitespace character.
 * @retval 1 @p character is a whitespace character.
 *
 * @note This function operates according to the standard C locale.
 *
 * @note Standard C function.
 */
int isspace(int character);

/**
 * Tries to parse @p string as an unsigned integer of the given @p base.
 *
 * This function will skip any and all whitespace characters (as determined by isspace()) at the beggining of the string.
 * It will stop on the first occurrence of a non-digit character. Note that what is considered a digit character depends on the base.
 * Digits start with '0' and end with 'Z'/'z'
 *
 * @param string                  The UTF-8 string of characters to parse.
 * @param string_length           The length of @p string, in bytes.
 * @param out_one_past_number_end Optional pointer in which a pointer to the next character after the end of the parsed region will be written.
 *                                e.g. if the string is "123bar" with base=10, this would be a pointer to 'b', so "bar".
 * @param base                    The base to use, in the range of 2 to 36 (inclusive).
 * @param out_integer             Pointer to an integer in which to write the result.
 *
 * @retval ferr_ok               The integer was successfully parsed.
 * @retval ferr_invalid_argument One or more of: 1) @p string was `NULL`, 2) @p out_one_past_number_end was `NULL`, 3) @p base was less than 2 or greater than 36, or 4) an error (which was not an overflow error) occurred while parsing the integer.
 * @retval ferr_too_big          The integer could not be represented (i.e. it overflowed). @p out_integer and @p out_one_past_number_end are not modified.
 *
 * @note This function is similar to the standard C `strto*` family of functions, but avoids being dependent on global variables like `errno`.
 *       However, unlike the `strto*` functions, it does NOT support prefixes.
 *
 * @note Non-standard function.
 */
ferr_t libk_string_to_integer_unsigned(const char* string, size_t string_length, const char** out_one_past_number_end, uint8_t base, uintmax_t* out_integer);

/**
 * Finds the first occurrence of @p character in @p string.
 *
 * @param string    The string to search through.
 * @param character The character to look for.
 *
 * @returns A pointer to the first occurrence of @p character in @p string, or `NULL` if none could be found.
 *
 * @note Standard C function.
 */
char* strchr(const char* string, int character);

/**
 * Finds the first occurrence of @p character in @p string, with @p string limited to a maximum of @p length characters.
 *
 * @param string    The string to search through.
 * @param character The character to look for.
 * @param length    The maximum number of characters from @p string to search through.
 *
 * @returns A pointer to the first occurrence of @p character in @p string within @p length characters, or `NULL` if none could be found.
 *
 * @note Non-standard function.
 */
char* strnchr(const char* string, int character, size_t length);

/**
 * Finds the first occurrence of any one of the characters from @p needle in @p haystack.
 *
 * @param haystack The string to search through.
 * @param needle   The list of characters to look for.
 *
 * @returns A pointer to the first occurrence of a character from @p needle in @p haystack, or `NULL` if none could be found.
 *
 * @note Standard C function.
 */
char* strpbrk(const char* haystack, const char* needle);

/**
 * Finds the first occurrence of any one of the characters from @p needle in @p haystack, with @p haystack limited to a maximum of @p length characters.
 *
 * @param haystack The string to search through.
 * @param needle   The list of characters to look for.
 * @param length    The maximum number of characters from @p haystack to search through.
 *
 * @returns A pointer to the first occurrence of a character from @p needle in @p haystack within @p length characters, or `NULL` if none could be found.
 *
 * @note Non-standard function.
 */
char* strnpbrk(const char* haystack, const char* needle, size_t length);

#define min(a, b) ({ \
		__typeof__(a) a_tmp = (a); \
		__typeof__(b) b_tmp = (b); \
		(a_tmp < b_tmp) ? a_tmp : b_tmp; \
	})

#define max(a, b) ({ \
		__typeof__(a) a_tmp = (a); \
		__typeof__(b) b_tmp = (b); \
		(a_tmp > b_tmp) ? a_tmp : b_tmp; \
	})

#endif // _LIBK_LIBK_H_
