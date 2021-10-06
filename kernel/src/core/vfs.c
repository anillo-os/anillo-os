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
 * Virtual filesystem abstraction layer.
 */

#include <ferro/core/vfs.h>
#include <ferro/core/vfs.backend.h>
#include <ferro/core/mempool.h>
#include <ferro/core/panic.h>
#include <ferro/core/locks.h>

#include <libk/libk.h>

FERRO_STRUCT(fvfs_mount) {
	void* context;
	const fvfs_backend_t* backend;
	uint64_t open_descriptor_count;

	size_t path_length;
	char path[];
};

static fvfs_mount_t** mounts = NULL;
static size_t mount_count = 0;
static size_t mounts_size = 0;
static flock_mutex_t mount_list_mutex = FLOCK_MUTEX_INIT;

static fvfs_mount_t* fvfs_mount_new(const char* path, size_t path_length, const fvfs_backend_t* backend, void* context) {
	fvfs_mount_t* result = NULL;

	flock_mutex_lock(&mount_list_mutex);

	if (mounts_size <= mount_count + 1) {
		size_t allocated_size = 0;
		if (fmempool_reallocate(mounts, sizeof(fvfs_mount_t*) * (mount_count + 1), &allocated_size, (void*)&mounts) != ferr_ok) {
			goto out;
		}
		mounts_size = allocated_size / sizeof(fvfs_mount_t*);
	}

	if (fmempool_allocate(sizeof(fvfs_mount_t) + path_length, NULL, (void*)&result) != ferr_ok) {
		goto out;
	}

	mounts[mount_count++] = result;

	result->backend = backend;
	result->context = context;
	result->open_descriptor_count = 0;
	result->path_length = 0;

	// rather than copying the path as-is, normalize it
	// yes, it's slower, but it guarantees certain assumptions made in other functions
	fvfs_path_component_t component;
	char* pos = result->path;
	for (ferr_t status = fvfs_path_component_first_n(path, path_length, &component); status == ferr_ok; status = fvfs_path_component_next(&component)) {
		if (pos != result->path) {
			pos[0] = '/';
			++pos;
			++result->path_length;
		}
		memcpy(pos, component.component, component.length);
		pos += component.length;
		result->path_length += component.length;
	}

out:
	flock_mutex_unlock(&mount_list_mutex);

	return result;
};

static void fvfs_mount_destroy(fvfs_mount_t* mount) {
	flock_mutex_lock(&mount_list_mutex);

	if (fmempool_free(mount) != ferr_ok) {
		fpanic("Failed to free mount descriptor");
	}

	for (size_t i = 0; i < mount_count; ++i) {
		if (mounts[i] == mount) {
			for (size_t j = i; j < mount_count - 1; ++j) {
				mounts[j] = mounts[j + 1];
			}
			--mount_count;
			break;
		}
	}

	flock_mutex_unlock(&mount_list_mutex);
};

FERRO_WUR static ferr_t fvfs_mount_open(fvfs_mount_t* mount) {
	uint64_t expected = __atomic_load_n(&mount->open_descriptor_count, __ATOMIC_RELAXED);

	while (expected != UINT64_MAX && !__atomic_compare_exchange_n(&mount->open_descriptor_count, &expected, expected + 1, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED));

	return expected == UINT64_MAX ? ferr_permanent_outage : ferr_ok;
};

static void fvfs_mount_close(fvfs_mount_t* mount) {
	__atomic_sub_fetch(&mount->open_descriptor_count, 1, __ATOMIC_RELAXED);
};

/**
 * Finds the mount for the given path and, if one was found, increases the open descriptor count on it.
 */
static fvfs_mount_t* fvfs_mount_open_for_path(const char* path, size_t path_length) {
	fvfs_mount_t* result = NULL;

	flock_mutex_lock(&mount_list_mutex);

	for (size_t i = 0; i < mount_count; ++i) {
		fvfs_mount_t* curr = mounts[i];
		fvfs_path_component_t mount_component;
		fvfs_path_component_t input_component;
		ferr_t mount_status = fvfs_path_component_first_n(curr->path, curr->path_length, &mount_component);
		ferr_t input_status = fvfs_path_component_first_n(path, path_length, &input_component);

		for (; mount_status == ferr_ok && input_status == ferr_ok; (mount_status = fvfs_path_component_next(&mount_component)), (input_status = fvfs_path_component_next(&input_component))) {
			if (!(
				mount_component.length == input_component.length &&
				strncmp(mount_component.component, input_component.component, mount_component.length) == 0
			)) {
				mount_status = ferr_cancelled;
				input_status = ferr_cancelled;
				break;
			}
		}

		// check if the mount path was not a prefix of the input path
		if (
			// if either of them are cancelled, they don't match up
			mount_status == ferr_cancelled ||
			input_status == ferr_cancelled ||

			// if the input path ran out of components while the mount path still had components left,
			// the mount path is longer
			(
				input_status == ferr_permanent_outage &&
				mount_status == ferr_ok
			)
		) {
			continue;
		}

		// otherwise, this mount is more specific and we want to use it instead of what we had before
		if (result) {
			fvfs_mount_close(result);
		}
		result = curr;
		if (fvfs_mount_open(result) != ferr_ok) {
			result = NULL;
			continue;
		}
	}

	flock_mutex_unlock(&mount_list_mutex);

	return result;
};

void fvfs_init(void) {};

ferr_t fvfs_descriptor_init(fvfs_descriptor_t* descriptor, fvfs_mount_t* mount, const char* path, size_t path_length, fvfs_descriptor_flags_t flags) {
	if (!path || !descriptor) {
		return ferr_invalid_argument;
	}

	char* copy = NULL;

	if (fmempool_allocate(sizeof(char) * path_length, NULL, (void*)&copy) != ferr_ok) {
		return ferr_temporary_outage;
	}

	memcpy(copy, path, sizeof(char) * path_length);

	descriptor->path = copy;
	descriptor->path_length = path_length;
	descriptor->flags = flags;
	descriptor->reference_count = 1;
	descriptor->mount = mount;

	return ferr_ok;
};

void fvfs_descriptor_destroy(fvfs_descriptor_t* descriptor) {
	if (fmempool_free(descriptor->path) != ferr_ok) {
		fpanic("Failed to free descriptor path");
	}
};

ferr_t fvfs_open_n(const char* path, size_t path_length, fvfs_descriptor_flags_t flags, fvfs_descriptor_t** out_descriptor) {
	fvfs_mount_t* mount;
	ferr_t status = ferr_ok;

	if (!path || !out_descriptor || !fvfs_path_is_absolute_n(path, path_length)) {
		return ferr_invalid_argument;
	}

	mount = fvfs_mount_open_for_path(path, path_length);

	if (!mount) {
		return ferr_no_such_resource;
	}

	status = mount->backend->open(mount->context, mount, path, path_length, flags, out_descriptor);
	if (status != ferr_ok) {
		return status;
	}

	fvfs_mount_close(mount);

	return status;
};

ferr_t fvfs_open(const char* path, fvfs_descriptor_flags_t flags, fvfs_descriptor_t** out_descriptor) {
	return fvfs_open_n(path, path ? strlen(path) : 0, flags, out_descriptor);
};

ferr_t fvfs_retain(fvfs_descriptor_t* descriptor) {
	if (__atomic_fetch_add(&descriptor->reference_count, 1, __ATOMIC_RELAXED) == 0) {
		return ferr_permanent_outage;
	}

	return ferr_ok;
};

void fvfs_release(fvfs_descriptor_t* descriptor) {
	if (__atomic_sub_fetch(&descriptor->reference_count, 1, __ATOMIC_ACQ_REL) != 0) {
		return;
	}

	descriptor->mount->backend->close(descriptor->mount->context, descriptor);
};

bool fvfs_path_is_absolute_n(const char* path, size_t path_length) {
	return path && path_length > 0 && path[0] == '/';
};

bool fvfs_path_is_absolute(const char* path) {
	return fvfs_path_is_absolute_n(path, path ? strlen(path) : 0);
};

ferr_t fvfs_path_component_first_n(const char* path, size_t path_length, fvfs_path_component_t* out_component) {
	const char* first_component = path;
	size_t first_component_length = path_length;
	const char* next_slash;

	if (!path || !out_component) {
		return ferr_invalid_argument;
	}

	while (first_component_length > 0 && *first_component == '/') {
		++first_component;
		--first_component_length;
	}

	if (first_component_length == 0) {
		return ferr_permanent_outage;
	}

	next_slash = strnchr(first_component, '/', first_component_length);

	if (next_slash) {
		first_component_length = next_slash - first_component;
	}

	out_component->entire_path = path;
	out_component->entire_path_length = path_length;
	out_component->component = first_component;
	out_component->length = first_component_length;

	return ferr_ok;
};

ferr_t fvfs_path_component_first(const char* path, fvfs_path_component_t* out_component) {
	return fvfs_path_component_first_n(path, path ? strlen(path) : 0, out_component);
};

ferr_t fvfs_path_component_next(fvfs_path_component_t* in_out_component) {
	const char* next_component;
	size_t next_component_length;
	const char* next_slash;

	if (!in_out_component) {
		return ferr_invalid_argument;
	}

	next_component = in_out_component->component + in_out_component->length;
	next_component_length = (in_out_component->entire_path + in_out_component->entire_path_length) - next_component;

	while (next_component_length > 0 && *next_component == '/') {
		++next_component;
		--next_component_length;
	}

	if (next_component_length == 0) {
		return ferr_permanent_outage;
	}

	next_slash = strnchr(next_component, '/', next_component_length);

	if (next_slash) {
		next_component_length = next_slash - next_component;
	}

	in_out_component->component = next_component;
	in_out_component->length = next_component_length;

	return ferr_ok;
};

// TODO: it should not be possible to create a mount on a floating path.
//       e.g. if '/foo' doesn't exist, it should be impossible to mount something at '/foo/bar'
ferr_t fvfs_mount(const char* path, size_t path_length, const fvfs_backend_t* backend, void* context) {
	fvfs_mount_t* mount = NULL;
	ferr_t status = ferr_ok;

	if (!path || !backend) {
		return ferr_invalid_argument;
	}

	flock_mutex_lock(&mount_list_mutex);

	mount = fvfs_mount_open_for_path(path, path_length);

	if (mount) {
		status = ferr_already_in_progress;
		fvfs_mount_close(mount);
		mount = NULL;
		goto out;
	}

	mount = fvfs_mount_new(path, path_length, backend, context);

	if (!mount) {
		status = ferr_temporary_outage;
	}

out:
	flock_mutex_unlock(&mount_list_mutex);

	return status;
};

ferr_t fvfs_unmount(const char* path, size_t path_length) {
	fvfs_mount_t* mount = NULL;
	ferr_t status = ferr_ok;
	uint64_t expected = 0;

	if (!path) {
		return ferr_invalid_argument;
	}

	flock_mutex_lock(&mount_list_mutex);

	mount = fvfs_mount_open_for_path(path, path_length);

	if (!mount) {
		status = ferr_no_such_resource;
		goto out;
	}

	if (!__atomic_compare_exchange_n(&mount->open_descriptor_count, &expected, UINT64_MAX, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
		status = ferr_already_in_progress;
		goto out;
	}

	fvfs_mount_destroy(mount);

out:
	flock_mutex_unlock(&mount_list_mutex);

	return status;
};

ferr_t fvfs_list_children_init(fvfs_descriptor_t* descriptor, fvfs_path_t* out_child_array, size_t child_array_count, bool absolute, size_t* out_listed_count, fvfs_list_children_context_t* out_context) {
	if (!descriptor || (!out_child_array && child_array_count > 0) || !out_listed_count ||!out_context) {
		return ferr_invalid_argument;
	}

	if (!descriptor->mount->backend->list_children_init || !descriptor->mount->backend->list_children || !descriptor->mount->backend->list_children_finish) {
		return ferr_unsupported;
	}

	return descriptor->mount->backend->list_children_init(descriptor->mount->context, descriptor, out_child_array, child_array_count, absolute, out_listed_count, out_context);
};

ferr_t fvfs_list_children(fvfs_descriptor_t* descriptor, fvfs_path_t* in_out_child_array, size_t child_array_count, bool absolute, size_t* in_out_listed_count, fvfs_list_children_context_t* in_out_context) {
	if (!descriptor || (!in_out_child_array && child_array_count > 0) || !in_out_listed_count || !in_out_context) {
		return ferr_invalid_argument;
	}

	if (!descriptor->mount->backend->list_children_init || !descriptor->mount->backend->list_children || !descriptor->mount->backend->list_children_finish) {
		return ferr_unsupported;
	}

	return descriptor->mount->backend->list_children(descriptor->mount->context, descriptor, in_out_child_array, child_array_count, absolute, in_out_listed_count, in_out_context);
};

ferr_t fvfs_list_children_finish(fvfs_descriptor_t* descriptor, fvfs_path_t* child_array, size_t listed_count, fvfs_list_children_context_t* in_out_context) {
	if (!descriptor || (!child_array && listed_count > 0) || !in_out_context) {
		return ferr_invalid_argument;
	}

	if (!descriptor->mount->backend->list_children_init || !descriptor->mount->backend->list_children || !descriptor->mount->backend->list_children_finish) {
		return ferr_unsupported;
	}

	return descriptor->mount->backend->list_children_finish(descriptor->mount->context, descriptor, child_array, listed_count, in_out_context);
};

ferr_t fvfs_copy_path(fvfs_descriptor_t* descriptor, bool absolute, char* out_path_buffer, size_t path_buffer_size, size_t* out_length) {
	ferr_t status = ferr_ok;

	if (!descriptor || (!out_path_buffer && path_buffer_size > 0)) {
		return ferr_invalid_argument;
	}

	if (!descriptor->mount->backend->copy_path) {
		return ferr_unsupported;
	}

	// the buffer is too small; simulate the NULL-0 case
	if (absolute && path_buffer_size < descriptor->mount->path_length) {
		path_buffer_size = descriptor->mount->path_length;
		out_path_buffer = NULL;
	}

	status = descriptor->mount->backend->copy_path(descriptor->mount->context, descriptor, absolute, out_path_buffer ? (absolute ? (out_path_buffer + descriptor->mount->path_length) : out_path_buffer) : NULL, path_buffer_size - (absolute ? descriptor->mount->path_length : 0), out_length);

	if (absolute) {
		*out_length += descriptor->mount->path_length;

		if (status == ferr_ok) {
			memcpy(out_path_buffer, descriptor->mount->path, descriptor->mount->path_length);
		}
	}

	return status;
};

ferr_t fvfs_copy_info(fvfs_descriptor_t* descriptor, fvfs_node_info_t* out_info) {
	if (!descriptor || !out_info) {
		return ferr_invalid_argument;
	}

	if (!descriptor->mount->backend->copy_info) {
		return ferr_unsupported;
	}

	return descriptor->mount->backend->copy_info(descriptor->mount->context, descriptor, out_info);
};

ferr_t fvfs_open_rn(fvfs_descriptor_t* base_descriptor, const char* path, size_t path_length, fvfs_descriptor_flags_t flags, fvfs_descriptor_t** out_descriptor) {
	size_t base_len = 0;
	char* abs_path = NULL;
	fvfs_node_info_t base_info;

	if (!path || path_length == 0) {
		return ferr_invalid_argument;
	}

	if (fvfs_path_is_absolute_n(path, path_length)) {
		return fvfs_open_n(path, path_length, flags, out_descriptor);
	}

	if (!out_descriptor || !base_descriptor) {
		return ferr_invalid_argument;
	}

	if (fvfs_copy_info(base_descriptor, &base_info) == ferr_unsupported) {
		return ferr_unsupported;
	}

	if (base_info.type != fvfs_node_type_directory) {
		return ferr_invalid_argument;
	}

	if (fvfs_copy_path(base_descriptor, true, NULL, 0, &base_len) == ferr_unsupported) {
		return ferr_unsupported;
	}

	// by adding `path_length`, we might be over-allocating, but there's no way we can be under-allocating
	// because the path being resolved is not an absolute path (we already took care of that) so:
	//   * it is relative
	//   * this implies that either:
	//     * it contains no '..' or '.' components
	//       (meaning `path` is just appended)
	//       OR
	//     * it contains one or more '..' or '.' components,
	//       which would shorten the path, not extend it
	//
	// `+1` for a slash
	if (fmempool_allocate(base_len + path_length + 1, NULL, (void*)&abs_path) != ferr_ok) {
		return ferr_temporary_outage;
	}

	if (fvfs_copy_path(base_descriptor, true, abs_path, base_len, &base_len) != ferr_ok) {
		fpanic_status(fmempool_free(abs_path));
		return ferr_temporary_outage;
	}

	char* abs_path_end = abs_path + base_len;

	fvfs_path_component_t component;
	for (ferr_t status = fvfs_path_component_first_n(path, path_length, &component); status == ferr_ok; status = fvfs_path_component_next(&component)) {
		if (component.length == 1 && component.component[0] == '.') {
			// ignore this component
			continue;
		}

		if (component.length == 2 && component.component[0] == '.' && component.component[1] == '.') {
			char* new_end = strrnchr(abs_path, '/', abs_path_end - abs_path);

			if (!new_end) {
				new_end = abs_path;
			}

			abs_path_end = new_end;
			continue;
		}

		abs_path_end[0] = '/';
		++abs_path_end;

		memcpy(abs_path_end, component.component, component.length);
		abs_path_end += component.length;
	}

	ferr_t status = fvfs_open_n(abs_path, abs_path_end - abs_path, flags, out_descriptor);

	fpanic_status(fmempool_free(abs_path));

	return status;
};

ferr_t fvfs_open_r(fvfs_descriptor_t* base_descriptor, const char* path, fvfs_descriptor_flags_t flags, fvfs_descriptor_t** out_descriptor) {
	return fvfs_open_rn(base_descriptor, path, path ? strlen(path) : 0, flags, out_descriptor);
};

ferr_t fvfs_read(fvfs_descriptor_t* descriptor, size_t offset, void* buffer, size_t buffer_size, size_t* out_read_count) {
	if (!descriptor) {
		return ferr_invalid_argument;
	}

	if (!descriptor->mount->backend->read) {
		return ferr_unsupported;
	}

	return descriptor->mount->backend->read(descriptor->mount->context, descriptor, offset, buffer, buffer_size, out_read_count);
};
