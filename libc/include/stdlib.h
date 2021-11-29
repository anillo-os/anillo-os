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

#ifndef _LIBC_STDLIB_H_
#define _LIBC_STDLIB_H_

#include <libc/base.h>

#include <stddef.h>
#include <stdint.h>

LIBC_DECLARATIONS_BEGIN;

LIBC_NO_RETURN void abort(void);
LIBC_NO_RETURN void exit(int status);
int atexit(void (*function)(void));

#define EXIT_SUCCESS 0
#define EXIT_FAILURE (-1)

int system(const char* command);
char* getenv(const char* name);

void* malloc(size_t size);
void* calloc(size_t element_count, size_t element_size);
void* realloc(void* old_pointer, size_t new_size);
void free(void* pointer);

double atof(const char* string);
int atoi(const char* string);
long atol(const char* string);
long long atoll(const char* string);
long strtol(const char* restrict string, char** restrict string_end, int base);
long long strtoll(const char* restrict string, char** restrict string_end, int base);
unsigned long strtoul(const char* restrict string, char** restrict string_end, int base);
unsigned long long strtoull(const char* restrict string, char** restrict string_end, int base);
float strtof(const char* restrict string, char** restrict string_end);
double strtod(const char* restrict string, char** restrict string_end);
long double strtold(const char* restrict string, char** restrict string_end);
intmax_t strtoimax(const char* restrict string, char** restrict string_end, int base);
uintmax_t strtoumax(const char* restrict string, char** restrict string_end, int base);

int mblen(const char* string, size_t max);
int mbtowc(wchar_t* restrict out_wide_char, const char* restrict string, size_t max);
size_t mbstowcs(wchar_t* restrict destination, const char* restrict source, size_t length);
int wctomb(char* out_string, wchar_t wide_char);
size_t wcstombs(char* restrict destination, const wchar_t* restrict source, size_t length);

#define MB_CUR_MAX ((void)0) /* TODO */

int rand(void);
void srand(unsigned seed);

#define RAND_MAX ((void)0) /* TODO */

void qsort(void* array, size_t count, size_t size, int (*comparator)(const void* first, const void* second));
void* bsearch(const void* key, const void* array, size_t count, size_t size, int (*comparator)(const void* first, const void* second));

LIBC_DECLARATIONS_END;

#endif // _LIBC_STDLIB_H_
