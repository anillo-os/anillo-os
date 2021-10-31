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

	hashmap->free(hashmap->callback_context, old_entry_array, sizeof(*old_entry_array) * old_size);

	hashmap->was_resized = true;

	return ferr_ok;
};

ferr_t simple_ghmap_init(simple_ghmap_t* hashmap, size_t initial_size, size_t data_size, simple_ghmap_allocate_f allocate, simple_ghmap_free_f free, simple_ghmap_hash_f hash, void* callback_context) {
	if (!hashmap || !allocate || !free) {
		return ferr_invalid_argument;
	}

	hashmap->was_resized = false;
	hashmap->in_use = 0;
	hashmap->size = initial_size;
	hashmap->data_size = data_size;
	hashmap->allocate = allocate;
	hashmap->free = free;
	hashmap->hash = hash;
	hashmap->callback_context = callback_context;

	if (hashmap->allocate(hashmap->callback_context, sizeof(*hashmap->entries) * hashmap->size, (void*)&hashmap->entries) != ferr_ok) {
		return ferr_temporary_outage;
	}

	simple_memset(hashmap->entries, 0, sizeof(*hashmap->entries) * hashmap->size);

	return ferr_ok;
};

void simple_ghmap_destroy(simple_ghmap_t* hashmap) {
	for (size_t i = 0; i < hashmap->size; ++i) {
		for (simple_ghmap_entry_t* entry = hashmap->entries[i]; entry != NULL; /* handled in the body */) {
			simple_ghmap_entry_t* next_entry = entry->next;

			hashmap->free(hashmap->callback_context, entry, sizeof(*entry) + hashmap->data_size);

			entry = next_entry;
		}
	}

	hashmap->free(hashmap->callback_context, hashmap->entries, sizeof(*hashmap->entries) * hashmap->size);
};

ferr_t simple_ghmap_lookup(simple_ghmap_t* hashmap, const void* key, size_t key_size, bool create_if_absent, bool* out_created, void** out_pointer) {
	if (!hashmap) {
		return ferr_invalid_argument;
	}

	if (!hashmap->hash) {
		return ferr_unsupported;
	}

	return simple_ghmap_lookup_h(hashmap, hashmap->hash(hashmap->callback_context, key, key_size), create_if_absent, out_created, out_pointer);
};

ferr_t simple_ghmap_lookup_h(simple_ghmap_t* hashmap, simple_ghmap_hash_t hash, bool create_if_absent, bool* out_created, void** out_pointer) {
	simple_ghmap_entry_t* new_entry = NULL;

	if (!hashmap || hash == SIMPLE_GHMAP_HASH_INVALID) {
		if (out_created) {
			*out_created = false;
		}
		return ferr_invalid_argument;
	}

	if (hashmap->size > 0) {
		for (simple_ghmap_entry_t* entry = hashmap->entries[hash % hashmap->size]; entry != NULL; entry = entry->next) {
			// if this is the entry we want, return it
			if (entry->hash == hash) {
				if (out_pointer) {
					*out_pointer = &entry->data[0];
				}
				if (out_created) {
					*out_created = false;
				}
				return ferr_ok;
			}
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

	if (hashmap->allocate(hashmap->callback_context, sizeof(*new_entry) + hashmap->data_size, (void*)&new_entry) != ferr_ok) {
		if (out_created) {
			*out_created = false;
		}
		return ferr_temporary_outage;
	}

	// we're definitely inserting it now

	if (out_created) {
		*out_created = true;
	}

	new_entry->hash = hash;

	++hashmap->in_use;

	// we're inserting a new entry, so now's the time to resize for efficiency (if necessary)
	simple_ghmap_resize(hashmap);

	simple_ghmap_insert(hashmap, new_entry);

	if (out_pointer) {
		*out_pointer = &new_entry->data[0];
	}

	return ferr_ok;
};

ferr_t simple_ghmap_clear(simple_ghmap_t* hashmap, const void* key, size_t key_size) {
	if (!hashmap) {
		return ferr_invalid_argument;
	}

	if (!hashmap->hash) {
		return ferr_unsupported;
	}

	return simple_ghmap_clear_h(hashmap, hashmap->hash(hashmap->callback_context, key, key_size));
};

ferr_t simple_ghmap_clear_h(simple_ghmap_t* hashmap, simple_ghmap_hash_t hash) {
	if (!hashmap || hash == SIMPLE_GHMAP_HASH_INVALID) {
		return ferr_invalid_argument;
	}

	if (hashmap->size > 0) {
		for (simple_ghmap_entry_t* entry = hashmap->entries[hash % hashmap->size]; entry != NULL; entry = entry->next) {
			if (entry->hash == hash) {
				simple_ghmap_entry_unlink(entry);
				hashmap->free(hashmap->callback_context, entry, sizeof(*entry) + hashmap->data_size);
				return ferr_ok;
			}
		}
	}

	return ferr_no_such_resource;
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
