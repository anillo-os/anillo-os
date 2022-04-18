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

#include <libsys/objects.private.h>
#include <libsys/mempool.h>
#include <libsys/abort.h>

ferr_t sys_object_init(sys_object_t* object, const sys_object_class_t* object_class) {
	ferr_t status = ferr_ok;

	if (!object_class || !object) {
		status = ferr_invalid_argument;
		goto out;
	}

	object->object_class = object_class;
	object->reference_count = 1;
	object->flags = 0;

out:
	return status;
};

void sys_object_destroy(sys_object_t* object) {
	if ((object->flags & sys_object_flag_free_on_destroy) != 0) {
		sys_abort_status(sys_mempool_free(object));
	}
};

ferr_t sys_object_retain(sys_object_t* object) {
	uint64_t old_value = __atomic_load_n(&object->reference_count, __ATOMIC_RELAXED);

	do {
		if (old_value == 0) {
			return ferr_permanent_outage;
		}
	} while (!__atomic_compare_exchange_n(&object->reference_count, &old_value, old_value + 1, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED));

	return ferr_ok;
};

void sys_object_release(sys_object_t* object) {
	uint64_t old_value = __atomic_load_n(&object->reference_count, __ATOMIC_RELAXED);

	do {
		if (old_value == 0) {
			return;
		}
	} while (!__atomic_compare_exchange_n(&object->reference_count, &old_value, old_value - 1, false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED));

	if (old_value != 1) {
		return;
	}

	if (object->object_class->destroy) {
		return object->object_class->destroy(object);
	} else {
		return sys_object_destroy(object);
	}
};

LIBSYS_WUR ferr_t sys_retain(sys_object_t* object) {
	if (object->object_class->retain) {
		return object->object_class->retain(object);
	} else {
		return sys_object_retain(object);
	}
};

void sys_release(sys_object_t* object) {
	if (object->object_class->release) {
		return object->object_class->release(object);
	} else {
		return sys_object_release(object);
	}
};

ferr_t sys_object_new(const sys_object_class_t* object_class, size_t extra_bytes, sys_object_t** out_object) {
	sys_object_t* object = NULL;
	ferr_t status = ferr_ok;

	if (!out_object || !object_class) {
		status = ferr_invalid_argument;
		goto out;
	}

	if (sys_mempool_allocate(sizeof(sys_object_t) + extra_bytes, NULL, (void*)&object)) {
		status = ferr_temporary_outage;
		goto out;
	}

	status = sys_object_init(object, object_class);
	if (status != ferr_ok) {
		goto out;
	}

	object->flags = sys_object_flag_free_on_destroy;

out:
	if (status == ferr_ok) {
		*out_object = object;
	} else {
		if (object != NULL) {
			sys_abort_status(sys_mempool_free(object));
		}
	}
	return status;
};
