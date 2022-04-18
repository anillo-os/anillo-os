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

#include <ferro/userspace/process-registry.h>
#include <ferro/core/ghmap.h>
#include <ferro/core/panic.h>
#include <ferro/core/locks.h>

typedef struct fprocreg_entry {
	fproc_t* process;
	fwaitq_waiter_t death_waiter;
} fprocreg_entry_t;

static simple_ghmap_t registry;
static flock_mutex_t registry_lock = FLOCK_MUTEX_INIT;
static fproc_id_t next_id = 1;

void fprocreg_init(void) {
	fpanic_status(simple_ghmap_init(&registry, 128, sizeof(fprocreg_entry_t), simple_ghmap_allocate_mempool, simple_ghmap_free_mempool, NULL, NULL, NULL, NULL, NULL, NULL));
};

static void fprocreg_process_died(void* context) {
	fproc_t* process = context;

	flock_mutex_lock(&registry_lock);
	if (simple_ghmap_clear_h(&registry, process->id) != ferr_ok) {
		// this means someone was unregistering the process right when it died.
		// this is acceptable; just refrain from releasing the process (since that
		// becomes fprocreg_unregister's responsibility)
		process = NULL;
	}
	flock_mutex_unlock(&registry_lock);

	if (process) {
		fproc_release(process);
	}
};

ferr_t fprocreg_register(fproc_t* process) {
	ferr_t status = ferr_ok;
	bool created = false;
	fprocreg_entry_t* stored = NULL;

	if (fproc_retain(process) != ferr_ok) {
		status = ferr_permanent_outage;
		goto out_unlocked;
	}

	flock_mutex_lock(&registry_lock);

	process->id = next_id;

	++next_id;
	if (next_id == 0 || next_id == FPROC_ID_INVALID) {
		next_id = 1;
	}

	status = simple_ghmap_lookup_h(&registry, process->id, true, sizeof(fprocreg_entry_t), &created, (void*)&stored, NULL);
	if (status != ferr_ok) {
		status = ferr_temporary_outage;
		goto out;
	}

	if (!created) {
		fpanic("process with `next_id` value already in registry");
	}

	stored->process = process;

	// register a waiter with the process so we can unregister it when it dies
	fwaitq_waiter_init(&stored->death_waiter, fprocreg_process_died, process);
	fwaitq_wait(&process->death_wait, &stored->death_waiter);

out:
	flock_mutex_unlock(&registry_lock);
out_unlocked:
	if (status != ferr_ok) {
		fproc_release(process);
	}
	return status;
};

ferr_t fprocreg_unregister(fproc_id_t id) {
	fprocreg_entry_t* entry = NULL;
	ferr_t status = ferr_ok;
	fproc_t* process = NULL;

	flock_mutex_lock(&registry_lock);

	status = simple_ghmap_lookup_h(&registry, id, false, 0, NULL, (void*)&entry, NULL);
	if (status != ferr_ok) {
		status = ferr_no_such_resource;
		goto out;
	}

	fwaitq_unwait(&entry->process->death_wait, &entry->death_waiter);

	process = entry->process;

	fpanic_status(simple_ghmap_clear_h(&registry, id));

out:
	flock_mutex_unlock(&registry_lock);
out_unlocked:
	if (process) {
		fproc_release(process);
	}
	return status;
};

ferr_t fprocreg_lookup(fproc_id_t id, bool retain, fproc_t** out_process) {
	ferr_t status = ferr_ok;
	fprocreg_entry_t* entry = NULL;

	if (retain && !out_process) {
		status = ferr_invalid_argument;
		goto out_unlocked;
	}

	flock_mutex_lock(&registry_lock);

	status = simple_ghmap_lookup_h(&registry, id, false, 0, NULL, (void*)&entry, NULL);
	if (status != ferr_ok) {
		status = ferr_no_such_resource;
		goto out;
	}

	if (retain) {
		status = fproc_retain(entry->process);
		if (status != ferr_ok) {
			goto out;
		}
	}

	if (out_process) {
		*out_process = entry->process;
	}

out:
	flock_mutex_unlock(&registry_lock);
out_unlocked:
	return status;
};
