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

#include <libsimple/ghmap.h>
#include <libsimple/libsimple.h>

static void simple_ghmap_entry_unlink(simple_ghmap_entry_t* entry) {
	*entry->prev = entry->next;
	if (entry->next) {
		entry->next->prev = entry->prev;
	}
};

static void simple_ghmap_insert(simple_ghmap_t* hashmap, simple_ghmap_entry_t* entry) {
	simple_ghmap_entry_t** prev = &hashmap->entries[entry->hash % hashmap->size];

	while (*prev != NULL) {
		prev = &(*prev)->next;
	}

	entry->prev = prev;
	entry->next = NULL;

	*prev = entry;
};

// this function does return error codes, but you can just ignore them safely.
static ferr_t simple_ghmap_resize(simple_ghmap_t* hashmap) {
	size_t old_size = hashmap->size;
	size_t new_size = old_size;
	simple_ghmap_entry_t** old_entry_array = hashmap->entries;
	simple_ghmap_entry_t** new_entry_array = NULL;

	// note that resizing the hashmap is only done for lookup efficiency.
	// if we fail to resize, that's not an issue, it just means lookups will be slower.
	//
	// however, if we fail to allocate an entry when creating a new entry, *that's* a problem.
	// if we fail to resize the array, chances are that there's not much free memory available,
	// so that serves as an indication that allocating new entries later on may fail.

	if (hashmap->in_use > hashmap->size / 2) {
		// more than half the entries are in-use; we should grow
		new_size = (old_size == 0 ? 1 : old_size) * 2;
	} else if (hashmap->was_resized && hashmap->in_use < hashmap->size / 8) {
		// less than an eighth of the entries are in-use (and this is not the initial size); we should shrink
		//
		// why an eighth and not a fourth? because if we resize when it's exactly one less than a fourth,
		// then the new smaller array will be one-less-than-a-half full, which may quickly require re-expansion.
		// so instead, only shrink when less than one eighth is used; that way, once we shrink, less than a fourth will be in-use.
		new_size = old_size / 2;
	}

	if (new_size == old_size) {
		// great, no need to do anything
		return ferr_ok;
	}

	if (hashmap->allocate(hashmap->callback_context, sizeof(*new_entry_array) * new_size, (void*)&new_entry_array) != ferr_ok) {
		return ferr_temporary_outage;
	}

	simple_memset(new_entry_array, 0, sizeof(*new_entry_array) * new_size);

	hashmap->entries = new_entry_array;
	hashmap->size = new_size;

	for (size_t i = 0; i < old_size; ++i) {
		for (simple_ghmap_entry_t* entry = old_entry_array[i]; entry != NULL; /* handled in the body */) {
			simple_ghmap_entry_t* next_entry = entry->next;

			simple_ghmap_entry_unlink(entry);
			simple_ghmap_insert(hashmap, entry);

			entry = next_entry;
		}
	}

	if (old_size > 0) {
		hashmap->free(hashmap->callback_context, old_entry_array, sizeof(*old_entry_array) * old_size);
	}

	hashmap->was_resized = true;

	return ferr_ok;
};

ferr_t simple_ghmap_init(simple_ghmap_t* hashmap, size_t initial_size, size_t default_data_size, simple_ghmap_allocate_f allocate, simple_ghmap_free_f free, simple_ghmap_hash_f hash, simple_ghmap_compares_equal_f compares_equal, simple_ghmap_stored_key_size_f stored_key_size, simple_ghmap_store_key_f store_key, simple_ghmap_clear_key_f clear_key, void* callback_context) {
	if (!hashmap || !allocate || !free) {
		return ferr_invalid_argument;
	}

	hashmap->was_resized = false;
	hashmap->in_use = 0;
	hashmap->size = initial_size;
	hashmap->default_data_size = default_data_size;
	hashmap->allocate = allocate;
	hashmap->free = free;
	hashmap->hash = hash;
	hashmap->compares_equal = compares_equal;
	hashmap->stored_key_size = stored_key_size;
	hashmap->store_key = store_key;
	hashmap->clear_key = clear_key;
	hashmap->callback_context = callback_context;

	if (hashmap->allocate(hashmap->callback_context, sizeof(*hashmap->entries) * hashmap->size, (void*)&hashmap->entries) != ferr_ok) {
		return ferr_temporary_outage;
	}

	simple_memset(hashmap->entries, 0, sizeof(*hashmap->entries) * hashmap->size);

	return ferr_ok;
};

static size_t simple_ghmap_entry_key_size(simple_ghmap_t* hashmap, simple_ghmap_entry_t* entry) {
	if (hashmap->compares_equal) {
		return *(size_t*)&entry->data[0];
	} else {
		return 0;
	}
};

static size_t simple_ghmap_entry_size(simple_ghmap_t* hashmap, simple_ghmap_entry_t* entry) {
	if (hashmap->compares_equal) {
		return sizeof(*entry) + entry->data_size + sizeof(size_t) + simple_ghmap_entry_key_size(hashmap, entry);
	} else {
		return sizeof(*entry) + entry->data_size;
	}
};

static size_t simple_ghmap_entry_size_tentative(simple_ghmap_t* hashmap, size_t key_size, size_t data_size) {
	if (hashmap->compares_equal) {
		return sizeof(simple_ghmap_entry_t) + sizeof(size_t) + key_size + data_size;
	} else {
		return sizeof(simple_ghmap_entry_t) + data_size;
	}
};

static void* simple_ghmap_entry_data(simple_ghmap_t* hashmap, simple_ghmap_entry_t* entry) {
	if (hashmap->compares_equal) {
		return &entry->data[0] + sizeof(size_t) + simple_ghmap_entry_key_size(hashmap, entry);
	} else {
		return &entry->data[0];
	}
};

static void* simple_ghmap_entry_key(simple_ghmap_t* hashmap, simple_ghmap_entry_t* entry) {
	if (hashmap->compares_equal) {
		return &entry->data[0] + sizeof(size_t);
	} else {
		return NULL;
	}
};

void simple_ghmap_destroy(simple_ghmap_t* hashmap) {
	for (size_t i = 0; i < hashmap->size; ++i) {
		for (simple_ghmap_entry_t* entry = hashmap->entries[i]; entry != NULL; /* handled in the body */) {
			simple_ghmap_entry_t* next_entry = entry->next;

			if (hashmap->clear_key) {
				hashmap->clear_key(hashmap->callback_context, simple_ghmap_entry_key(hashmap, entry), simple_ghmap_entry_key_size(hashmap, entry));
			}

			hashmap->free(hashmap->callback_context, entry, simple_ghmap_entry_size(hashmap, entry));

			entry = next_entry;
		}
	}

	if (hashmap->size > 0) {
		hashmap->free(hashmap->callback_context, hashmap->entries, sizeof(*hashmap->entries) * hashmap->size);
	}
};

static ferr_t simple_ghmap_lookup_internal(simple_ghmap_t* hashmap, simple_ghmap_hash_t hash, const void* key, size_t key_size, bool create_if_absent, size_t size_if_absent, bool* out_created, void** out_pointer, size_t* out_size, const void** out_stored_key_pointer, size_t* out_stored_key_size) {
	simple_ghmap_entry_t* new_entry = NULL;
	size_t stored_key_size = key_size;
	size_t data_size = size_if_absent;

	if (!hashmap || hash == SIMPLE_GHMAP_HASH_INVALID) {
		if (out_created) {
			*out_created = false;
		}
		return ferr_invalid_argument;
	}

	if (data_size == SIZE_MAX) {
		data_size = hashmap->default_data_size;
	}

	if (hashmap->size > 0) {
		for (simple_ghmap_entry_t* entry = hashmap->entries[hash % hashmap->size]; entry != NULL; entry = entry->next) {
			// if the hashes aren't equal, this isn't the entry we want
			if (entry->hash != hash) {
				continue;
			}

			// if we store keys, make sure this is actually the entry we want (and not just a hash collision)
			if (hashmap->compares_equal && !hashmap->compares_equal(hashmap->callback_context, simple_ghmap_entry_key(hashmap, entry), simple_ghmap_entry_key_size(hashmap, entry), key, key_size)) {
				continue;
			}

			if (out_pointer) {
				*out_pointer = simple_ghmap_entry_data(hashmap, entry);
			}
			if (out_created) {
				*out_created = false;
			}
			if (out_size) {
				*out_size = entry->data_size;
			}
			if (out_stored_key_pointer) {
				*out_stored_key_pointer = simple_ghmap_entry_key(hashmap, entry);
			}
			if (out_stored_key_size) {
				*out_stored_key_size = simple_ghmap_entry_key_size(hashmap, entry);
			}
			return ferr_ok;
		}
	}

	// if we got here, the entry doesn't exist.

	// if we don't want to create it when it doesn't exist, return here.
	if (!create_if_absent) {
		if (out_created) {
			*out_created = false;
		}
		return ferr_no_such_resource;
	}

	if (hashmap->stored_key_size) {
		stored_key_size = hashmap->stored_key_size(hashmap->callback_context, key, key_size);
	}

	if (hashmap->allocate(hashmap->callback_context, simple_ghmap_entry_size_tentative(hashmap, stored_key_size, data_size), (void*)&new_entry) != ferr_ok) {
		if (out_created) {
			*out_created = false;
		}
		return ferr_temporary_outage;
	}

	// copy in the key (if necessary)
	if (hashmap->compares_equal) {
		*(size_t*)&new_entry->data[0] = stored_key_size;

		if (hashmap->store_key) {
			if (hashmap->store_key(hashmap->callback_context, key, key_size, simple_ghmap_entry_key(hashmap, new_entry), stored_key_size) != ferr_ok) {
				hashmap->free(hashmap->callback_context, new_entry, simple_ghmap_entry_size(hashmap, new_entry));
				return ferr_unknown;
			}
		} else if (stored_key_size < key_size) {
			hashmap->free(hashmap->callback_context, new_entry, simple_ghmap_entry_size(hashmap, new_entry));
			return ferr_unknown;
		} else {
			simple_memcpy(simple_ghmap_entry_key(hashmap, new_entry), key, stored_key_size);
		}
	}

	// we're definitely inserting it now

	if (out_created) {
		*out_created = true;
	}

	new_entry->hash = hash;
	new_entry->data_size = data_size;

	++hashmap->in_use;

	// we're inserting a new entry, so now's the time to resize for efficiency (if necessary)
	simple_ghmap_resize(hashmap);

	simple_ghmap_insert(hashmap, new_entry);

	if (out_pointer) {
		*out_pointer = simple_ghmap_entry_data(hashmap, new_entry);
	}

	if (out_size) {
		*out_size = data_size;
	}

	if (out_stored_key_pointer) {
		*out_stored_key_pointer = simple_ghmap_entry_key(hashmap, new_entry);
	}

	if (out_stored_key_size) {
		*out_stored_key_size = simple_ghmap_entry_key_size(hashmap, new_entry);
	}

	return ferr_ok;
};

ferr_t simple_ghmap_lookup(simple_ghmap_t* hashmap, const void* key, size_t key_size, bool create_if_absent, size_t size_if_absent, bool* out_created, void** out_pointer, size_t* out_size) {
	if (!hashmap) {
		return ferr_invalid_argument;
	}

	if (!hashmap->hash) {
		return ferr_unsupported;
	}

	return simple_ghmap_lookup_internal(hashmap, hashmap->hash(hashmap->callback_context, key, key_size), key, key_size, create_if_absent, size_if_absent, out_created, out_pointer, out_size, NULL, NULL);
};

ferr_t simple_ghmap_lookup_h(simple_ghmap_t* hashmap, simple_ghmap_hash_t hash, bool create_if_absent, size_t size_if_absent, bool* out_created, void** out_pointer, size_t* out_size) {
	if (!hashmap) {
		return ferr_invalid_argument;
	}

	if (hashmap->compares_equal) {
		return ferr_unsupported;
	}

	return simple_ghmap_lookup_internal(hashmap, hash, NULL, 0, create_if_absent, size_if_absent, out_created, out_pointer, out_size, NULL, NULL);
};

static ferr_t simple_ghmap_clear_internal(simple_ghmap_t* hashmap, simple_ghmap_hash_t hash, const void* key, size_t key_size) {
	if (!hashmap || hash == SIMPLE_GHMAP_HASH_INVALID) {
		return ferr_invalid_argument;
	}

	if (hashmap->size > 0) {
		for (simple_ghmap_entry_t* entry = hashmap->entries[hash % hashmap->size]; entry != NULL; entry = entry->next) {
			if (entry->hash != hash) {
				continue;
			}

			if (hashmap->compares_equal && !hashmap->compares_equal(hashmap->callback_context, simple_ghmap_entry_key(hashmap, entry), simple_ghmap_entry_key_size(hashmap, entry), key, key_size)) {
				continue;
			}

			simple_ghmap_entry_unlink(entry);

			if (hashmap->clear_key) {
				hashmap->clear_key(hashmap->callback_context, simple_ghmap_entry_key(hashmap, entry), simple_ghmap_entry_key_size(hashmap, entry));
			}

			hashmap->free(hashmap->callback_context, entry, simple_ghmap_entry_size(hashmap, entry));
			return ferr_ok;
		}
	}

	return ferr_no_such_resource;
};

ferr_t simple_ghmap_clear(simple_ghmap_t* hashmap, const void* key, size_t key_size) {
	if (!hashmap) {
		return ferr_invalid_argument;
	}

	if (!hashmap->hash) {
		return ferr_unsupported;
	}

	return simple_ghmap_clear_internal(hashmap, hashmap->hash(hashmap->callback_context, key, key_size), key, key_size);
};

ferr_t simple_ghmap_clear_h(simple_ghmap_t* hashmap, simple_ghmap_hash_t hash) {
	if (!hashmap) {
		return ferr_invalid_argument;
	}

	if (hashmap->compares_equal) {
		return ferr_unsupported;
	}

	return simple_ghmap_clear_internal(hashmap, hash, NULL, 0);
};

ferr_t simple_ghmap_for_each(simple_ghmap_t* hashmap, simple_ghmap_iterator_f iterator, void* context) {
	for (size_t i = 0; i < hashmap->size; ++i) {
		for (simple_ghmap_entry_t* entry = hashmap->entries[i]; entry != NULL; entry = entry->next) {
			if (!iterator(context, hashmap, entry->hash, simple_ghmap_entry_key(hashmap, entry), simple_ghmap_entry_key_size(hashmap, entry), simple_ghmap_entry_data(hashmap, entry), entry->data_size)) {
				return ferr_cancelled;
			}
		}
	}

	return ferr_ok;
};

ferr_t simple_ghmap_clear_all(simple_ghmap_t* hashmap) {
	for (size_t i = 0; i < hashmap->size; ++i) {
		for (simple_ghmap_entry_t* entry = hashmap->entries[i]; entry != NULL; /* handled in the body */) {
			simple_ghmap_entry_t* next_entry = entry->next;

			if (hashmap->clear_key) {
				hashmap->clear_key(hashmap->callback_context, simple_ghmap_entry_key(hashmap, entry), simple_ghmap_entry_key_size(hashmap, entry));
			}

			hashmap->free(hashmap->callback_context, entry, simple_ghmap_entry_size(hashmap, entry));

			entry = next_entry;
		}
	}

	if (hashmap->size > 0) {
		hashmap->free(hashmap->callback_context, hashmap->entries, sizeof(*hashmap->entries) * hashmap->size);
	}

	hashmap->entries = NULL;
	hashmap->size = 0;
	hashmap->in_use = 0;
	hashmap->was_resized = true;

	return ferr_ok;
};

size_t simple_ghmap_entry_count(simple_ghmap_t* hashmap) {
	return hashmap->in_use;
};

ferr_t simple_ghmap_lookup_stored_key(simple_ghmap_t* hashmap, const void* key, size_t key_size, const void** out_stored_key_pointer, size_t* out_stored_key_size) {
	if (!hashmap) {
		return ferr_invalid_argument;
	}

	if (!hashmap->hash || !hashmap->compares_equal) {
		return ferr_unsupported;
	}

	return simple_ghmap_lookup_internal(hashmap, hashmap->hash(hashmap->callback_context, key, key_size), key, key_size, false, SIZE_MAX, NULL, NULL, NULL, out_stored_key_pointer, out_stored_key_size);
};

#define FNV_64_PRIME        (1099511628211ULL)
#define FNV_64_OFFSET_BASIS (14695981039346656037ULL)

simple_ghmap_hash_t simple_ghmap_hash_string(void* context, const void* key, size_t key_size) {
	// this implementation uses the FNV-1 algorithm with 64-bit parameters

	simple_ghmap_hash_t hash = FNV_64_OFFSET_BASIS;
	const char* string = key;

	if (key_size == SIZE_MAX) {
		key_size = simple_strlen(string);
	}

	while (key_size-- > 0) {
		hash = (hash * FNV_64_PRIME) ^ *(string++);
	}

	return hash;
};

bool simple_ghmap_compares_equal_string(void* context, const void* stored_key, size_t stored_key_size, const void* key, size_t key_size) {
	if (stored_key_size == SIZE_MAX) {
		stored_key_size = simple_strlen(stored_key);
	}

	if (key_size == SIZE_MAX) {
		key_size = simple_strlen(key);
	}

	if (stored_key_size != key_size) {
		return false;
	}

	return simple_strncmp(stored_key, key, stored_key_size) == 0;
};

size_t simple_ghmap_stored_key_size_string(void* context, const void* key_to_store, size_t key_to_store_size) {
	if (key_to_store_size == SIZE_MAX) {
		key_to_store_size = simple_strlen(key_to_store);
	}
	return key_to_store_size;
};

ferr_t simple_ghmap_store_key_string(void* context, const void* key_to_store, size_t key_to_store_size, void* buffer, size_t buffer_size) {
	if (key_to_store_size == SIZE_MAX) {
		key_to_store_size = simple_strlen(key_to_store);
	}

	if (buffer_size < key_to_store_size) {
		return ferr_too_big;
	}

	simple_memcpy(buffer, key_to_store, key_to_store_size);
	return ferr_ok;
};

ferr_t simple_ghmap_init_string_to_generic(simple_ghmap_t* hashmap, size_t initial_size, size_t data_size, simple_ghmap_allocate_f allocate, simple_ghmap_free_f free, void* callback_context) {
	return simple_ghmap_init(hashmap, initial_size, data_size, allocate, free, simple_ghmap_hash_string, simple_ghmap_compares_equal_string, simple_ghmap_stored_key_size_string, simple_ghmap_store_key_string, NULL, callback_context);
};
