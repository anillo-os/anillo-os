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
 * A generic hashmap implementation.
 */

#ifndef _LIBSIMPLE_GHMAP_H_
#define _LIBSIMPLE_GHMAP_H_

#include <stddef.h>
#include <stdbool.h>

#include <libsimple/base.h>
#include <ferro/error.h>

LIBSIMPLE_DECLARATIONS_BEGIN;

typedef size_t simple_ghmap_hash_t;

#define SIMPLE_GHMAP_HASH_INVALID SIZE_MAX

/**
 * A memory allocation callback for ghmaps.
 *
 * @param context     The callback context given to the hashmap that is calling this function.
 * @param bytes       The size (in bytes) of how much memory to allocate.
 * @param out_pointer A pointer into which a pointer to the newly allocated memory should be written (upon success).
 *
 * @retval ferr_ok               The allocation succeeded; a pointer to the newly allocated memory has been written into @p out_pointer.
 * @retval ferr_temporary_outage The allocation failed.
 */
typedef ferr_t (*simple_ghmap_allocate_f)(void* context, size_t bytes, void** out_pointer);

/**
 * A memory release callback for ghmaps.
 *
 * @param context The callback context given to the hashmap that is calling this function.
 * @param pointer A pointer to the start of the memory that was allocated.
 * @param bytes   The size of the memory (in bytes) that was allocated.
 */
typedef void (*simple_ghmap_free_f)(void* context, void* pointer, size_t bytes);

/**
 * A key hashing callback for ghmaps.
 *
 * @param context  The callback context given to the hashmap that is calling this function.
 * @param key      The key to be hashed. How it is interpretted is decided by this function on its own.
 * @param key_size The size of the key to be hashed. This is typically a byte size, but again, how it is interpretted is decided by this function on its own.
 *
 * Upon encountering an error (e.g. an invalid key), this function should return ::SIMPLE_GHMAP_HASH_INVALID.
 *
 * @returns A hash value that uniquely identifies this key (as determined by whatever this function decides is "unique").
 *          That is, the hash value should satisfy the following conditions:
 *            1. It should always return the same hash value for the same key (as determined by whatever this function decides is the "same" key).
 *            2. Different keys should return different hash values (as determined by however this function decides whether keys are "different").
 */
typedef simple_ghmap_hash_t (*simple_ghmap_hash_f)(void* context, const void* key, size_t key_size);

/**
 * Checks whether the input key is equal to the stored key and returns `true` if it is or `false` otherwise.
 */
typedef bool (*simple_ghmap_compares_equal_f)(void* context, const void* stored_key, size_t stored_key_size, const void* key, size_t key_size);

typedef size_t (*simple_ghmap_stored_key_size_f)(void* context, const void* key_to_store, size_t key_to_store_size);

typedef ferr_t (*simple_ghmap_store_key_f)(void* context, const void* key_to_store, size_t key_to_store_size, void* buffer, size_t buffer_size);

typedef void (*simple_ghmap_clear_key_f)(void* context, const void* stored_key, size_t stored_key_size);

LIBSIMPLE_STRUCT(simple_ghmap_entry) {
	simple_ghmap_entry_t** prev;
	simple_ghmap_entry_t* next;
	simple_ghmap_hash_t hash;
	size_t data_size;

	/**
	 * For hash-only maps (that is, hashmaps that don't store keys), this is just the user data.
	 * For hash-and-key maps (that is, hashmaps that also store keys), this is the size of the stored key (as a ::size_t), followed immediately by the key, and then followed immediately by the user data.
	 */
	char data[];
};

LIBSIMPLE_STRUCT(simple_ghmap) {
	bool was_resized;
	size_t size;
	size_t in_use;
	size_t default_data_size;

	simple_ghmap_allocate_f allocate;
	simple_ghmap_free_f free;
	simple_ghmap_hash_f hash;
	simple_ghmap_compares_equal_f compares_equal;
	simple_ghmap_stored_key_size_f stored_key_size;
	simple_ghmap_store_key_f store_key;
	simple_ghmap_clear_key_f clear_key;

	void* callback_context;

	/**
	 * Array of pointers to entries.
	 *
	 * This is done this way, rather than having all initial entries be directly in an array,
	 * so that the array can be resized without invalidating pointers to the data in the hashmap.
	 *
	 * That way, only two functions are needed:
	 *   - One to retrieve a pointer to the data (and optionally create it if it doesn't exist), and
	 *   - One to clear the data
	 *
	 * This makes repeated use of the data much simpler and makes it require fewer lookups
	 * (just one initial lookup and then continued use of the pointer).
	 */
	simple_ghmap_entry_t** entries;
};

typedef bool (*simple_ghmap_iterator_f)(void* context, simple_ghmap_t* hashmap, simple_ghmap_hash_t hash, const void* key, size_t key_size, void* entry, size_t entry_size);

/**
 * Initializes a new generic hashmap.
 *
 * @param hashmap           The hashmap to initialize
 * @param initial_size      The number of entries to make room for initially.
 * @param default_data_size The default size of each data entry in the hashmap.
 * @param allocate          A memory allocation function to use for memory needed by this hashmap.
 * @param free              A memory release function to use for memory needed by this hashmap.
 * @param hash              A hash generation function to use for generating hashes from keys.
 *                          If this is `NULL`, the hashmap will not be able to generate hashes from keys and only the `simple_ghmap_*_h` variant functions can be used for working with entries in it.
 * @param compares_equal    A key comparison function to use for checking whether 2 keys are equal.
 *                          If this is `NULL`, the hashmap will not store keys, only hash values.
 *                          If this is NOT `NULL`, the hashmap will store keys and call this comparison function when 2 hashes match to make sure
 *                          the keys are actually equal and it's not just a hash collision.
 * @param stored_key_size   A stored-key size determination function; this will be called when a key needs to be stored, in order to determine the size of the buffer needed to store the key.
 *                          If this is `NULL`, the input key size is always used as the stored key size.
 * @param store_key         A key storage function used to store keys in the hashmap.
 *                          If this is `NULL`, the input key is always copied byte-wise into the hashmap.
 * @param clear_key         A key deletion function used to clear keys when clearing hashmap entries.
 *                          If this is `NULL`, no action is taken (the key data is simply freed along with the rest of the entry).
 * @param callback_context  An optional context to pass to all the callback functions (i.e. `allocate`, `free`, `hash`, etc.).
 *
 * @retval ferr_ok               The hashmap was successfully initialized.
 * @retval ferr_temporary_outage There were insufficient resources to initialize the hashmap.
 * @retval ferr_invalid_argument One or more of: 1) @p hashmap was `NULL`, 2) @p allocate was `NULL`, 3) @p free was `NULL`.
 */
LIBSIMPLE_WUR ferr_t simple_ghmap_init(simple_ghmap_t* hashmap, size_t initial_size, size_t default_data_size, simple_ghmap_allocate_f allocate, simple_ghmap_free_f free, simple_ghmap_hash_f hash, simple_ghmap_compares_equal_f compares_equal, simple_ghmap_stored_key_size_f stored_key_size, simple_ghmap_store_key_f store_key, simple_ghmap_clear_key_f clear_key, void* callback_context);

/**
 * Destroys and releases all resources held by the given hashmap.
 *
 * @param hashmap The hashmap to destroy.
 */
void simple_ghmap_destroy(simple_ghmap_t* hashmap);

/**
 * Looks up the entry for the given key, optionally creating it if it is not already in the hashmap.
 *
 * @param hashmap          The hashmap to lookup the key in.
 * @param key              The key to look up. Exact format varies depending on the hash function used by the hashmap.
 * @param key_size         The size of the key (typically in bytes).
 * @param create_if_absent Whether to create a new entry for the given key in the hashmap if none is found.
 * @param size_if_absent   The size of the entry if it needs to be created; if this is `SIZE_MAX`, the default entry size is used.
 * @param out_created      Optional pointer in which the result of whether a new entry was created is stored.
 *                         This is useful to know whether the data is already valid (if it was not created now) or if you have to initialize it (if it *was* created now).
 * @param out_pointer      Optional pointer in which a pointer to the data associated with the given key will be written.
 *                         The pointer written into this pointer is valid for as long as the entry is valid; that is, the only reason it can become invalid is through simple_ghmap_clear() (or its `h` variant).
 *
 * @retval ferr_ok               The entry was found (or created) and a pointer to its data was written into @p out_pointer.
 * @retval ferr_temporary_outage Can only occur if @p create_if_absent is `true`; indicates that there were insufficient resources to create the new entry.
 * @retval ferr_invalid_argument One or more of: 1) @p hashmap was `NULL`, 2) the given key was invalid for this hashmap (as determined by its hash function).
 * @retval ferr_unsupported      This hashmap does not support key hashing; only the `h` variant of this function may be used.
 * @retval ferr_no_such_resource Can only occur if @p create_if_absent is `false`; indicates that no entry was found for the given key in the given hashmap.
 */
LIBSIMPLE_WUR ferr_t simple_ghmap_lookup(simple_ghmap_t* hashmap, const void* key, size_t key_size, bool create_if_absent, size_t size_if_absent, bool* out_created, void** out_pointer, size_t* out_size);

/**
 * Similar to simple_ghmap_lookup(), but the lookup is performed with the given hash, rather than the computed hash of a key.
 *
 * @see simple_ghmap_lookup
 *
 * Returns all the same error codes as simple_ghmap_lookup().
 * The difference is that, for this function, ferr_unsupported means that the hashmap stores keys and doesn't support hash-only lookup.
 */
LIBSIMPLE_WUR ferr_t simple_ghmap_lookup_h(simple_ghmap_t* hashmap, simple_ghmap_hash_t hash, bool create_if_absent, size_t size_if_absent, bool* out_created, void** out_pointer, size_t* out_size);

/**
 * Deletes/clears the entry for the given key in the given hashmap (if it exists).
 *
 * @param hashmap  The hashmap to clear the key from.
 * @param key      The key to clear. Exact format varies depending on the hash function used by the hashmap.
 * @param key_size The size of the key (typically in bytes).
 *
 * @retval ferr_ok               The entry was found in the hashmap and has now been cleared/removed/deleted/eliminated.
 * @retval ferr_no_such_resource No entry was found for the given key in the given hashmap.
 * @retval ferr_unsupported      This hashmap does not support key hashing; only the `h` variant of this function may be used.
 */
LIBSIMPLE_WUR ferr_t simple_ghmap_clear(simple_ghmap_t* hashmap, const void* key, size_t key_size);

/**
 * Similar to simple_ghmap_clear(), but the lookup is performed with the given hash, rather than the computed hash of a key.
 *
 * @see simple_ghmap_clear
 *
 * Returns all the same error codes as simple_ghmap_clear().
 * The difference is that, for this function, ferr_unsupported means that the hashmap stores keys and doesn't support hash-only lookup.
 */
LIBSIMPLE_WUR ferr_t simple_ghmap_clear_h(simple_ghmap_t* hashmap, simple_ghmap_hash_t hash);

ferr_t simple_ghmap_for_each(simple_ghmap_t* hashmap, simple_ghmap_iterator_f iterator, void* context);

LIBSIMPLE_WUR ferr_t simple_ghmap_clear_all(simple_ghmap_t* hashmap);

size_t simple_ghmap_entry_count(simple_ghmap_t* hashmap);

LIBSIMPLE_WUR ferr_t simple_ghmap_lookup_stored_key(simple_ghmap_t* hashmap, const void* key, size_t key_size, const void** out_stored_key_pointer, size_t* out_stored_key_size);

/**
 * An implementation of ::simple_ghmap_hash_f for strings (both null- and non-null-terminated).
 *
 * @param key      A string to hash.
 * @param key_size The length of the string in bytes.
 *                 If this is ::SIZE_MAX, the string is considered a null-terminated string and its length is determined with simple_strlen().
 *                 Otherwise, the string is considered non-null-terminated and its length is the given size; null characters are counted as part of the string.
 */
simple_ghmap_hash_t simple_ghmap_hash_string(void* context, const void* key, size_t key_size);

/**
 * An implementation of ::simple_ghmap_compares_equal_f for strings (both null- and non-null-terminated).
 */
bool simple_ghmap_compares_equal_string(void* context, const void* stored_key, size_t stored_key_size, const void* key, size_t key_size);

size_t simple_ghmap_stored_key_size_string(void* context, const void* key_to_store, size_t key_to_store_size);

ferr_t simple_ghmap_store_key_string(void* context, const void* key_to_store, size_t key_to_store_size, void* buffer, size_t buffer_size);

LIBSIMPLE_WUR ferr_t simple_ghmap_init_string_to_generic(simple_ghmap_t* hashmap, size_t initial_size, size_t data_size, simple_ghmap_allocate_f allocate, simple_ghmap_free_f free, void* callback_context);

simple_ghmap_hash_t simple_ghmap_hash_data(void* context, const void* key, size_t key_size);

bool simple_ghmap_compares_equal_data(void* context, const void* stored_key, size_t stored_key_size, const void* key, size_t key_size);

LIBSIMPLE_DECLARATIONS_END;

#endif // _LIBSIMPLE_GHMAP_H_
