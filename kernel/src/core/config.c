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
 * The configuration subsystem.
 */

#include <ferro/core/config.h>
#include <ferro/core/mempool.h>
#include <ferro/core/panic.h>
#include <libk/libk.h>

// TODO: a hash table would be more efficient
FERRO_STRUCT(fconfig_entry) {
	const char* key;
	size_t key_length;
	const char* value;
	size_t value_length;
};
static fconfig_entry_t* config_entries = NULL;
static size_t config_entry_count = 0;

void fconfig_init(const char* data, size_t length) {
	const char* line = data;

	while (length > 0 && line) {
		const char* eol = strnpbrk(line, "\r\n", length);
		size_t line_length = eol ? (eol - line) : length;

		const char* key = NULL;
		size_t key_length = 0;
		const char* value = NULL;
		size_t value_length = 0;

		const char* equal_sign = strnchr(line, '=', line_length);

		if (equal_sign) {
			key = line;

			// skip leading whitespace in the key
			while (isspace(*key)) {
				++key;
			}
			key_length = equal_sign - key;

			// skip trailing whitespace in the key
			while (key_length > 0 && isspace(key[key_length - 1])) {
				--key_length;
			}

			value = equal_sign + 1;

			// skip leading whitespace in the value
			while (isspace(*value)) {
				++value;
			}
			value_length = (line_length - (equal_sign - key)) - 1;

			// the input data may be filled with tons of null terminators at the end,
			// so determine the real length if we're at the end of the input data.
			if (!eol) {
				value_length = strnlen(value, value_length);
			}

			// skip trailing whitespace in the value
			while (value_length > 0 && isspace(value[value_length - 1])) {
				--value_length;
			}

			if (fmempool_reallocate(config_entries, sizeof(*config_entries) * (config_entry_count + 1), NULL, (void*)&config_entries) != ferr_ok) {
				fpanic("Failed to allocate memory for configuration entries");
			}

			++config_entry_count;

			config_entries[config_entry_count - 1].key = key;
			config_entries[config_entry_count - 1].key_length = key_length;
			config_entries[config_entry_count - 1].value = value;
			config_entries[config_entry_count - 1].value_length = value_length;
		}

		line = eol;
		length -= line_length;
		if (line) {
			while (*line == '\r' || *line == '\n') {
				++line;
				--length;
			}
			if (*line == '\0') {
				line = NULL;
				length = 0;
			}
		}
	}
};

ferr_t fconfig_get(const char* key, char** out_value) {
	size_t orig_len = 0;
	const char* orig = fconfig_get_nocopy(key, &orig_len);
	char* new = NULL;

	if (!orig) {
		return ferr_no_such_resource;
	}

	if (!out_value) {
		return ferr_ok;
	}

	if (fmempool_allocate((orig_len + 1) * sizeof(char), NULL, (void*)&new) != ferr_ok) {
		return ferr_temporary_outage;
	}

	memcpy(new, orig, orig_len);
	new[orig_len] = '\0';

	*out_value = new;

	return ferr_ok;
};

const char* fconfig_get_nocopy(const char* key, size_t* out_value_length) {
	for (size_t i = 0; i < config_entry_count; ++i) {
		fconfig_entry_t* entry = &config_entries[i];

		if (strncmp(entry->key, key, entry->key_length) == 0) {
			if (out_value_length) {
				*out_value_length = entry->value_length;
			}
			return entry->value;
		}
	}

	return NULL;
};
