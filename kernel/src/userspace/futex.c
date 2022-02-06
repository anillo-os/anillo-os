/*
 * This file is part of Anillo OS
 * Copyright (C) 2022 Anillo OS Developers
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

#include <ferro/userspace/futex.h>

FERRO_STRUCT(futex_table_key) {
	uintptr_t address;
	uint64_t channel;
};

static simple_ghmap_hash_t futex_table_key_hash(void* context, const void* key, size_t key_size) {
	if (key_size != sizeof(futex_table_key_t)) {
		return SIMPLE_GHMAP_HASH_INVALID;
	}

	// most futexes have a single channel, so use the address itself as the hash;
	// hash collisions will be resolved with futex_table_key_equal
	return ((const futex_table_key_t*)key)->address;
};

static bool futex_table_key_equal(void* context, const void* stored_key, size_t stored_key_size, const void* key, size_t key_size) {
	if (stored_key_size != sizeof(futex_table_key_t) || key_size != sizeof(futex_table_key_t)) {
		return SIMPLE_GHMAP_HASH_INVALID;
	}

	const futex_table_key_t* stored_table_key = stored_key;
	const futex_table_key_t* table_key = key;

	return stored_table_key->address == table_key->address && stored_table_key->channel == table_key->address;
};

ferr_t futex_table_init(futex_table_t* table) {
	ferr_t status = ferr_ok;

	status = simple_ghmap_init(&table->table, 16, sizeof(futex_t), simple_ghmap_allocate_mempool, simple_ghmap_free_mempool, futex_table_key_hash, futex_table_key_equal, NULL, NULL, NULL, NULL);
	if (status != ferr_ok) {
		goto out;
	}

	flock_mutex_init(&table->mutex);

out:
	return status;
};

void futex_table_destroy(futex_table_t* table) {
	flock_mutex_lock(&table->mutex);
	simple_ghmap_destroy(&table->table);
	flock_mutex_unlock(&table->mutex);
};

ferr_t futex_lookup(futex_table_t* table, uintptr_t address, uint64_t channel, futex_t** out_futex) {
	futex_t* futex = NULL;
	bool created = false;
	ferr_t status = ferr_ok;

	futex_table_key_t key = {
		.address = address,
		.channel = channel,
	};

retry:
	flock_mutex_lock(&table->mutex);
	status = simple_ghmap_lookup(&table->table, &key, sizeof(key), true, sizeof(futex_t), &created, (void*)&futex, NULL);
	if (status == ferr_ok) {
		if (created) {
			futex->table = table;
			futex->address = address;
			futex->channel = channel;
			frefcount_init(&futex->reference_count);
			fwaitq_init(&futex->waitq);
		} else {
			if (frefcount_increment(&futex->reference_count) != ferr_ok) {
				// if it's dead, unlock the table and try again; we're going to have to create it again once it's destroyed.
				// TODO: optimize this by reinitializing it here and having futex_release() check for this
				//       we're probably going to have to add a generation counter to do this safely.
				flock_mutex_unlock(&table->mutex);
				goto retry;
			}
		}
	}
	flock_mutex_unlock(&table->mutex);

	return status;
};

void futex_release(futex_t* futex) {
	if (frefcount_decrement(&futex->reference_count) != ferr_permanent_outage) {
		return;
	}

	futex_table_key_t key = {
		.address = futex->address,
		.channel = futex->channel,
	};

	// TODO: for debugging, we should check whether anyone is still waiting on the futex.
	//       if they are, this is clearly an error, since every waiter should be holding a reference on the futex.

	flock_mutex_lock(&futex->table->mutex);
	FERRO_WUR_IGNORE(simple_ghmap_clear(&futex->table->table, &key, sizeof(key)));
	flock_mutex_unlock(&futex->table->mutex);
};
