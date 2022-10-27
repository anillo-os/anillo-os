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
 * Minimalistic libc-like utility functions for both kernel-space and userspace.
 */

#include <stdint.h>

#include <libsimple/libsimple.h>

void* simple_memclone(void* restrict destination, const void* restrict source, size_t n, size_t m) {
	char* destbuf = destination;
	while (m-- > 0) {
		simple_memcpy(destbuf, source, n);
		destbuf += n;
	}
	return destination;
};

size_t simple_strlen(const char* string) {
	// simple_strlen is used often enough that it's worth it to have it be completely separate from simple_strnlen (rather than calling simple_strnlen with a SIZE_MAX limit)
	size_t count = 0;
	while (*(string++) != '\0')
		++count;
	return count;
};

size_t simple_strnlen(const char* string, size_t max_length) {
	size_t count = 0;
	while (max_length > 0 && *string != '\0') {
		++string;
		++count;
		--max_length;
	}
	return count;
};

int simple_strncmp(const char* first, const char* second, size_t n) {
	while (n-- > 0) {
		char first_char = *(first++);
		char second_char = *(second++);

		if (first_char < second_char) {
			return -1;
		} else if (first_char > second_char) {
			return 1;
		} else if (first_char == '\0') {
			return 0;
		}
	}

	return 0;
};

int simple_memcmp(const void* _first, const void* _second, size_t n) {
	const uint8_t* first = _first;
	const uint8_t* second = _second;

	while (n-- > 0) {
		uint8_t first_char = *(first++);
		uint8_t second_char = *(second++);

		if (first_char < second_char) {
			return -1;
		} else if (first_char > second_char) {
			return 1;
		}
	}

	return 0;
};

int simple_isspace(int character) {
	switch (character) {
		case ' ':
		case '\t':
		case '\n':
		case '\v':
		case '\f':
		case '\r':
			return 1;
		default:
			return 0;
	}
};

/**
 * @returns The value of the given digit in the given base, or `UINT8_MAX` otherwise.
 */
FERRO_ALWAYS_INLINE uint8_t digit_value_for_base(char digit, uint8_t base) {
	uint8_t value = 0;

	if (digit >= '0' && digit <= '9') {
		value = digit - '0';
	} else if (digit >= 'a' && digit <= 'z') {
		value = (digit - 'a') + 10;
	} else if (digit >= 'A' && digit <= 'Z') {
		value = (digit - 'A') + 10;
	} else {
		return UINT8_MAX;
	}

	if (value >= base) {
		return UINT8_MAX;
	} else {
		return value;
	}
};

ferr_t simple_string_to_integer_unsigned(const char* string, size_t string_length, const char** out_one_past_number_end, uint8_t base, uintmax_t* out_integer) {
	uintmax_t result = 0;
	ferr_t status = ferr_invalid_argument;

	if (!string || !out_integer || base < 2 || base > 36) {
		return ferr_invalid_argument;
	}

	while (string_length > 0 && simple_isspace(*string)) {
		++string;
		--string_length;
	}

	while (string_length > 0) {
		uint8_t value = digit_value_for_base(*string, base);

		if (value == UINT8_MAX) {
			break;
		}

		if (__builtin_mul_overflow(result, base, &result)) {
			status = ferr_too_big;
			break;
		}

		if (__builtin_add_overflow(result, value, &result)) {
			status = ferr_too_big;
			break;
		}

		// once we have at least one digit parsed, we can return success.
		status = ferr_ok;

		--string_length;
		++string;
	}

	if (status == ferr_ok) {
		*out_integer = result;
		if (out_one_past_number_end) {
			*out_one_past_number_end = string;
		}
	}

	return status;
};

char* simple_strchr(const char* string, int character) {
	return simple_strnchr(string, character, SIZE_MAX);
};

char* simple_strnchr(const char* string, int character, size_t length) {
	if (character == '\0') {
		return (char*)(string + simple_strnlen(string, length));
	}

	while (length > 0 && *string != '\0') {
		if (*string == character) {
			return (char*)string;
		}
		++string;
		--length;
	}

	return NULL;
};

char* simple_strrchr(const char* string, int character) {
	return simple_strrnchr(string, character, simple_strlen(string));
};

char* simple_strrnchr(const char* string, int character, size_t length) {
	const char* pos = string + (length - 1);

	if (character == '\0') {
		return (char*)(string + simple_strnlen(string, length));
	}

	while (length > 0 && pos >= string) {
		if (*pos == character) {
			return (char*)pos;
		}
		--pos;
		--length;
	}

	return NULL;
};

char* simple_strpbrk(const char* haystack, const char* needle) {
	return simple_strnpbrk(haystack, needle, SIZE_MAX);
};

char* simple_strnpbrk(const char* haystack, const char* needle, size_t length) {
	size_t needle_length = simple_strlen(needle);

	while (length > 0 && *haystack != '\0') {
		for (size_t i = 0; i < needle_length; ++i) {
			if (*haystack == needle[i]) {
				return (char*)haystack;
			}
		}
		++haystack;
		--length;
	}

	return NULL;
};

void simple_bzero(void* buffer, size_t count) {
	simple_memset(buffer, 0, count);
};
