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

#include <ferro/core/per-cpu.private.h>
#include <ferro/core/mempool.h>
#include <ferro/core/panic.h>
#include <libsimple/libsimple.h>

#define DEFAULT_ENTRY_COUNT 64

void fper_cpu_init(void) {
	// TODO: initialize for each CPU
	fper_cpu_main_table_t* main_table_ptr = fper_cpu_main_table_pointer();

	// needs to be prebound because interrupts are disabled here
	if (fmempool_allocate_advanced(sizeof(fper_cpu_entry_t) * DEFAULT_ENTRY_COUNT, 0, UINT8_MAX, fmempool_flag_prebound, NULL, (void*)&main_table_ptr->entries) != ferr_ok) {
		fpanic("Failed to allocate entry array");
	}

	main_table_ptr->entry_count = DEFAULT_ENTRY_COUNT;

	simple_memset(main_table_ptr->entries, 0, sizeof(fper_cpu_entry_t) * main_table_ptr->entry_count);
};

FERRO_ALWAYS_INLINE bool fper_cpu_key_is_valid_fast(fper_cpu_key_t key) {
	uint32_t key_flags = key >> 32;

	// the key must have the "registered" flag set
	// and must not have any other flags set
	if ((key_flags & fper_cpu_entry_is_registered) == 0 || (key_flags & ~fper_cpu_entry_is_registered) != 0) {
		return false;
	}

	return true;
};

static bool fper_cpu_key_is_valid_slow(fper_cpu_key_t key) {
	fper_cpu_main_table_t* main_table_ptr;
	fper_cpu_small_key_t small_key = key & UINT32_MAX;
	uint32_t key_flags = key >> 32;
	fper_cpu_entry_t* entry;

	// the key must have the "registered" flag set
	// and must not have any other flags set
	if ((key_flags & fper_cpu_entry_is_registered) == 0 || (key_flags & ~fper_cpu_entry_is_registered) != 0) {
		return false;
	}

	main_table_ptr = fper_cpu_main_table_pointer();

	// the key can't be bigger than the table
	if (small_key >= main_table_ptr->entry_count) {
		return false;
	}

	entry = &main_table_ptr->entries[small_key];

	if ((entry->flags & fper_cpu_entry_is_registered) == 0) {
		return false;
	}

	return true;
};

#if SLOW_KEY_CHECK
	#define fper_cpu_key_is_valid fper_cpu_key_is_valid_slow
#else
	#define fper_cpu_key_is_valid fper_cpu_key_is_valid_fast
#endif

// TODO: this needs to register the key in all the processors' data tables
ferr_t fper_cpu_register(fper_cpu_key_t* out_key) {
	fper_cpu_main_table_t* main_table_ptr;

	if (!out_key) {
		return ferr_invalid_argument;
	}

	main_table_ptr = fper_cpu_main_table_pointer();

	for (size_t i = 0; i < main_table_ptr->entry_count; ++i) {
		fper_cpu_entry_t* entry = &main_table_ptr->entries[i];

		if ((entry->flags & fper_cpu_entry_is_registered) != 0) {
			continue;
		}

		entry->flags = fper_cpu_entry_is_registered;
		entry->key = i & UINT32_MAX;

		*out_key = ((uint64_t)(fper_cpu_entry_is_registered) << 32ULL) | (i & UINT32_MAX);

		return ferr_ok;
	}

	return ferr_temporary_outage;
};

ferr_t fper_cpu_unregister(fper_cpu_key_t key, bool skip_previous_destructor) {
	fper_cpu_main_table_t* main_table_ptr;
	fper_cpu_small_key_t small_key = key & UINT32_MAX;
	uint32_t key_flags = key >> 32;
	fper_cpu_entry_t* entry;

	if (!fper_cpu_key_is_valid(key)) {
		return ferr_invalid_argument;
	}

	main_table_ptr = fper_cpu_main_table_pointer();
	entry = &main_table_ptr->entries[small_key];

	if (!skip_previous_destructor && (entry->flags & fper_cpu_entry_flag_has_value) != 0 && entry->destructor) {
		entry->destructor(entry->destructor_context, entry->data);
	}

	simple_memset(entry, 0, sizeof(*entry));

	return ferr_ok;
};

ferr_t fper_cpu_read(fper_cpu_key_t key, fper_cpu_data_t* out_data) {
	fper_cpu_main_table_t* main_table_ptr;
	fper_cpu_small_key_t small_key = key & UINT32_MAX;
	uint32_t key_flags = key >> 32;
	fper_cpu_entry_t* entry;

	if (!fper_cpu_key_is_valid(key)) {
		return ferr_invalid_argument;
	}

	main_table_ptr = fper_cpu_main_table_pointer();
	entry = &main_table_ptr->entries[small_key];

	if ((entry->flags & fper_cpu_entry_flag_has_value) == 0) {
		return ferr_no_such_resource;
	}

	if (out_data) {
		*out_data = entry->data;
	}

	return ferr_ok;
};

ferr_t fper_cpu_write(fper_cpu_key_t key, fper_cpu_data_t data, fper_cpu_data_destructor_f destructor, void* destructor_context, bool skip_previous_destructor) {
	fper_cpu_main_table_t* main_table_ptr;
	fper_cpu_small_key_t small_key = key & UINT32_MAX;
	uint32_t key_flags = key >> 32;
	fper_cpu_entry_t* entry;

	if (!fper_cpu_key_is_valid(key)) {
		return ferr_invalid_argument;
	}

	main_table_ptr = fper_cpu_main_table_pointer();
	entry = &main_table_ptr->entries[small_key];

	if (!skip_previous_destructor && (entry->flags & fper_cpu_entry_flag_has_value) != 0 && entry->destructor) {
		entry->destructor(entry->destructor_context, entry->data);
	}

	entry->flags |= fper_cpu_entry_flag_has_value;
	entry->data = data;
	entry->destructor = destructor;
	entry->destructor_context = destructor_context;

	return ferr_ok;
};

ferr_t fper_cpu_clear(fper_cpu_key_t key, bool skip_previous_destructor) {
	fper_cpu_main_table_t* main_table_ptr;
	fper_cpu_small_key_t small_key = key & UINT32_MAX;
	uint32_t key_flags = key >> 32;
	fper_cpu_entry_t* entry;

	if (!fper_cpu_key_is_valid(key)) {
		return ferr_invalid_argument;
	}

	main_table_ptr = fper_cpu_main_table_pointer();
	entry = &main_table_ptr->entries[small_key];

	if ((entry->flags & fper_cpu_entry_flag_has_value) == 0) {
		return ferr_no_such_resource;
	}

	if (!skip_previous_destructor && entry->destructor) {
		entry->destructor(entry->destructor_context, entry->data);
	}

	entry->flags &= ~fper_cpu_entry_flag_has_value;
	entry->data = 0;
	entry->destructor = NULL;
	entry->destructor_context = NULL;

	return ferr_ok;
};
