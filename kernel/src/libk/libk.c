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
 * Minimalistic library for kernel-space utilities.
 */

#include <stdint.h>

#include <libk/libk.h>

void* memcpy(void* restrict destination, const void* restrict source, size_t n) {
	if (destination == source) {
		return destination;
	}
	char* destbuf = destination;
	const char* srcbuf = source;
	while (n-- > 0) {
		*(destbuf++) = *(srcbuf++);
	}
	return destination;
};

void* memclone(void* restrict destination, const void* restrict source, size_t n, size_t m) {
	char* destbuf = destination;
	while (m-- > 0) {
		memcpy(destbuf, source, n);
		destbuf += n;
	}
	return destination;
};

size_t strlen(const char* string) {
	// strlen is used often enough that it's worth it to have it be completely separate from strnlen (rather than calling strnlen with a SIZE_MAX limit)
	size_t count = 0;
	while (*(string++) != '\0')
		++count;
	return count;
};

size_t strnlen(const char* string, size_t max_length) {
	size_t count = 0;
	while (max_length > 0 && *string != '\0') {
		++string;
		++count;
		--max_length;
	}
	return count;
};

void* memmove(void* destination, const void* source, size_t n) {
	if (destination == source || n == 0) {
		return destination;
	} else if (destination < source) {
		char* destbuf = destination;
		const char* srcbuf = source;
		while (n-- > 0)
			*(destbuf++) = *(srcbuf++);
	} else if (destination > source) {
		char* destbuf = (char*)destination + n - 1;
		const char* srcbuf = (char*)source + n - 1;
		while (n-- > 0)
			*(destbuf--) = *(srcbuf--);
	}
	return destination;
};

void* memset(void* destination, int value, size_t n) {
	unsigned char* destbuf = destination;
	while (n-- > 0)
		*(destbuf++) = (unsigned char)value;
	return destination;
};

int strncmp(const char* first, const char* second, size_t n) {
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

int memcmp(const void* _first, const void* _second, size_t n) {
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

int isspace(int character) {
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

ferr_t libk_string_to_integer_unsigned(const char* string, size_t string_length, const char** out_one_past_number_end, uint8_t base, uintmax_t* out_integer) {
	uintmax_t result = 0;
	ferr_t status = ferr_invalid_argument;

	if (!string || !out_integer || base < 2 || base > 36) {
		return ferr_invalid_argument;
	}

	while (string_length > 0 && isspace(*string)) {
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

char* strchr(const char* string, int character) {
	return strnchr(string, character, SIZE_MAX);
};

char* strnchr(const char* string, int character, size_t length) {
	if (character == '\0') {
		return (char*)(string + strnlen(string, length));
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

char* strrchr(const char* string, int character) {
	return strrnchr(string, character, strlen(string));
};

char* strrnchr(const char* string, int character, size_t length) {
	const char* pos = string + (length - 1);

	if (character == '\0') {
		return (char*)(string + strnlen(string, length));
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

char* strpbrk(const char* haystack, const char* needle) {
	return strnpbrk(haystack, needle, SIZE_MAX);
};

char* strnpbrk(const char* haystack, const char* needle, size_t length) {
	size_t needle_length = strlen(needle);

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
