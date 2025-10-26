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

#include <vfs.server.h>
#include <libsys/general.private.h>
#include <libsimple/libsimple.h>
#include <vfsman/vfs.h>
#include <vfsman/vfs.backend.h>
#include <vfsman/ramdisk.h>
#include <libsys/pages.private.h>
#include <libspooky/proxy.private.h>
#include <vfsman/vfs.backend.private.h>

static vfsman_mount_t** mounts = NULL;
static size_t mount_count = 0;
static size_t mounts_size = 0;
static sys_mutex_t mount_list_mutex = SYS_MUTEX_INIT;

static vfsman_mount_t* vfsman_mount_new_locked(const char* path, size_t path_length, const vfsman_backend_t* backend, void* context) {
	vfsman_mount_t* result = NULL;

	if (mounts_size <= mount_count + 1) {
		size_t allocated_size = 0;
		if (sys_mempool_reallocate(mounts, sizeof(vfsman_mount_t*) * (mount_count + 1), &allocated_size, (void*)&mounts) != ferr_ok) {
			goto out;
		}
		mounts_size = allocated_size / sizeof(vfsman_mount_t*);
	}

	if (sys_mempool_allocate(sizeof(vfsman_mount_t) + path_length, NULL, (void*)&result) != ferr_ok) {
		goto out;
	}

	mounts[mount_count++] = result;

	result->backend = backend;
	result->context = context;
	result->open_descriptor_count = 0;
	result->path_length = 0;

	// rather than copying the path as-is, normalize it
	// yes, it's slower, but it guarantees certain assumptions made in other functions
	sys_path_component_t component;
	char* pos = result->path;
	for (ferr_t status = sys_path_component_first_n(path, path_length, &component); status == ferr_ok; status = sys_path_component_next(&component)) {
		if (pos != result->path) {
			pos[0] = '/';
			++pos;
			++result->path_length;
		}
		simple_memcpy(pos, component.component, component.length);
		pos += component.length;
		result->path_length += component.length;
	}

out:
	return result;
};

static vfsman_mount_t* vfsman_mount_new(const char* path, size_t path_length, const vfsman_backend_t* backend, void* context) {
	vfsman_mount_t* result;
	eve_mutex_lock(&mount_list_mutex);
	result = vfsman_mount_new_locked(path, path_length, backend, context);
	sys_mutex_unlock(&mount_list_mutex);
	return result;
};

static void vfsman_mount_destroy_locked(vfsman_mount_t* mount) {
	LIBVFS_WUR_IGNORE(sys_mempool_free(mount));

	for (size_t i = 0; i < mount_count; ++i) {
		if (mounts[i] == mount) {
			for (size_t j = i; j < mount_count - 1; ++j) {
				mounts[j] = mounts[j + 1];
			}
			--mount_count;
			break;
		}
	}
};

static void vfsman_mount_destroy(vfsman_mount_t* mount) {
	eve_mutex_lock(&mount_list_mutex);
	vfsman_mount_destroy_locked(mount);
	sys_mutex_unlock(&mount_list_mutex);
};

FERRO_WUR static ferr_t vfsman_mount_open(vfsman_mount_t* mount) {
	uint64_t expected = __atomic_load_n(&mount->open_descriptor_count, __ATOMIC_RELAXED);

	while (expected != UINT64_MAX && !__atomic_compare_exchange_n(&mount->open_descriptor_count, &expected, expected + 1, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED));

	return expected == UINT64_MAX ? ferr_permanent_outage : ferr_ok;
};

static void vfsman_mount_close(vfsman_mount_t* mount) {
	__atomic_sub_fetch(&mount->open_descriptor_count, 1, __ATOMIC_RELAXED);
};

/**
 * Finds the mount for the given path and, if one was found, increases the open descriptor count on it.
 */
static vfsman_mount_t* vfsman_mount_open_for_path_locked(const char* path, size_t path_length) {
	vfsman_mount_t* result = NULL;

	for (size_t i = 0; i < mount_count; ++i) {
		vfsman_mount_t* curr = mounts[i];
		sys_path_component_t mount_component;
		sys_path_component_t input_component;
		ferr_t mount_status = sys_path_component_first_n(curr->path, curr->path_length, &mount_component);
		ferr_t input_status = sys_path_component_first_n(path, path_length, &input_component);

		for (; mount_status == ferr_ok && input_status == ferr_ok; (mount_status = sys_path_component_next(&mount_component)), (input_status = sys_path_component_next(&input_component))) {
			if (!(
				mount_component.length == input_component.length &&
				simple_strncmp(mount_component.component, input_component.component, mount_component.length) == 0
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
			vfsman_mount_close(result);
		}
		result = curr;
		if (vfsman_mount_open(result) != ferr_ok) {
			result = NULL;
			continue;
		}
	}

	return result;
};

static vfsman_mount_t* vfsman_mount_open_for_path(const char* path, size_t path_length) {
	vfsman_mount_t* result;
	eve_mutex_lock(&mount_list_mutex);
	result = vfsman_mount_open_for_path_locked(path, path_length);
	sys_mutex_unlock(&mount_list_mutex);
	return result;
};

void vfsman_init(void) {};

static void vfsman_descriptor_destroy(vfsman_descriptor_t* descriptor) {
	sys_object_destroy(descriptor);
};

static const vfsman_object_class_t descriptor_class = {
	LIBSYS_OBJECT_CLASS_INTERFACE(NULL),
	.destroy = vfsman_descriptor_destroy,
};

const vfsman_object_class_t* vfsman_object_class_descriptor(void) {
	return &descriptor_class;
};

ferr_t vfsman_descriptor_new(vfsman_mount_t* mount, vfsman_descriptor_flags_t flags, size_t extra_bytes, vfsman_descriptor_t** out_descriptor) {
	vfsman_descriptor_object_t* descriptor = NULL;
	ferr_t status = ferr_ok;

	status = sys_object_new(&descriptor_class, sizeof(*descriptor) - sizeof(descriptor->object) + extra_bytes, (void*)&descriptor);
	if (status != ferr_ok) {
		goto out;
	}

	descriptor->flags = flags;
	descriptor->mount = mount;
	descriptor->internal_context = NULL;

out:
	if (status == ferr_ok) {
		*out_descriptor = (void*)descriptor;
	} else if (descriptor) {
		vfsman_release((void*)descriptor);
	}
	return status;
};

ferr_t vfsman_open_n(const char* path, size_t path_length, vfsman_descriptor_flags_t flags, vfsman_descriptor_t** out_descriptor) {
	vfsman_mount_t* mount;
	ferr_t status = ferr_ok;

	if (!path || !out_descriptor || !sys_path_is_absolute_n(path, path_length)) {
		return ferr_invalid_argument;
	}

	mount = vfsman_mount_open_for_path(path, path_length);

	if (!mount) {
		return ferr_no_such_resource;
	}

	status = mount->backend->open(mount->context, mount, path, path_length, flags, out_descriptor);
	if (status != ferr_ok) {
		return status;
	}

	vfsman_mount_close(mount);

	return status;
};

ferr_t vfsman_open(const char* path, vfsman_descriptor_flags_t flags, vfsman_descriptor_t** out_descriptor) {
	return vfsman_open_n(path, path ? simple_strlen(path) : 0, flags, out_descriptor);
};

ferr_t vfsman_retain(vfsman_object_t* obj) {
	return sys_retain(obj);
};

void vfsman_release(vfsman_object_t* obj) {
	sys_release(obj);
};

const vfsman_object_class_t* vfsman_object_class(vfsman_object_t* object) {
	return sys_object_class(object);
};

// TODO: it should not be possible to create a mount on a floating path.
//       e.g. if '/foo' doesn't exist, it should be impossible to mount something at '/foo/bar'
ferr_t vfsman_mount(const char* path, size_t path_length, const vfsman_backend_t* backend, void* context) {
	vfsman_mount_t* mount = NULL;
	ferr_t status = ferr_ok;

	if (!path || !backend) {
		return ferr_invalid_argument;
	}

	eve_mutex_lock(&mount_list_mutex);

	mount = vfsman_mount_open_for_path_locked(path, path_length);

	if (mount) {
		status = ferr_already_in_progress;
		vfsman_mount_close(mount);
		mount = NULL;
		goto out;
	}

	mount = vfsman_mount_new_locked(path, path_length, backend, context);

	if (!mount) {
		status = ferr_temporary_outage;
	}

out:
	sys_mutex_unlock(&mount_list_mutex);

	return status;
};

ferr_t vfsman_unmount(const char* path, size_t path_length) {
	vfsman_mount_t* mount = NULL;
	ferr_t status = ferr_ok;
	uint64_t expected = 0;

	if (!path) {
		return ferr_invalid_argument;
	}

	eve_mutex_lock(&mount_list_mutex);

	mount = vfsman_mount_open_for_path_locked(path, path_length);

	if (!mount) {
		status = ferr_no_such_resource;
		goto out;
	}

	if (!__atomic_compare_exchange_n(&mount->open_descriptor_count, &expected, UINT64_MAX, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
		status = ferr_already_in_progress;
		goto out;
	}

	vfsman_mount_destroy_locked(mount);

out:
	sys_mutex_unlock(&mount_list_mutex);

	return status;
};

ferr_t vfsman_list_children_init(vfsman_descriptor_t* obj, sys_path_t* out_child_array, size_t child_array_count, bool absolute, size_t* out_listed_count, vfsman_list_children_context_t* out_context) {
	vfsman_descriptor_object_t* descriptor = (void*)obj;

	if (!descriptor || (!out_child_array && child_array_count > 0) || !out_listed_count ||!out_context) {
		return ferr_invalid_argument;
	}

	if (!descriptor->mount->backend->list_children_init || !descriptor->mount->backend->list_children || !descriptor->mount->backend->list_children_finish) {
		return ferr_unsupported;
	}

	return descriptor->mount->backend->list_children_init(descriptor->mount->context, obj, out_child_array, child_array_count, absolute, out_listed_count, out_context);
};

ferr_t vfsman_list_children(vfsman_descriptor_t* obj, sys_path_t* in_out_child_array, size_t child_array_count, bool absolute, size_t* in_out_listed_count, vfsman_list_children_context_t* in_out_context) {
	vfsman_descriptor_object_t* descriptor = (void*)obj;

	if (!descriptor || (!in_out_child_array && child_array_count > 0) || !in_out_listed_count || !in_out_context) {
		return ferr_invalid_argument;
	}

	if (!descriptor->mount->backend->list_children_init || !descriptor->mount->backend->list_children || !descriptor->mount->backend->list_children_finish) {
		return ferr_unsupported;
	}

	return descriptor->mount->backend->list_children(descriptor->mount->context, obj, in_out_child_array, child_array_count, absolute, in_out_listed_count, in_out_context);
};

ferr_t vfsman_list_children_finish(vfsman_descriptor_t* obj, sys_path_t* child_array, size_t listed_count, vfsman_list_children_context_t* in_out_context) {
	vfsman_descriptor_object_t* descriptor = (void*)obj;

	if (!descriptor || (!child_array && listed_count > 0) || !in_out_context) {
		return ferr_invalid_argument;
	}

	if (!descriptor->mount->backend->list_children_init || !descriptor->mount->backend->list_children || !descriptor->mount->backend->list_children_finish) {
		return ferr_unsupported;
	}

	return descriptor->mount->backend->list_children_finish(descriptor->mount->context, obj, child_array, listed_count, in_out_context);
};

ferr_t vfsman_copy_path(vfsman_descriptor_t* obj, bool absolute, char* out_path_buffer, size_t path_buffer_size, size_t* out_length) {
	ferr_t status = ferr_ok;
	vfsman_descriptor_object_t* descriptor = (void*)obj;

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

	status = descriptor->mount->backend->copy_path(descriptor->mount->context, obj, absolute, out_path_buffer ? (absolute ? (out_path_buffer + descriptor->mount->path_length) : out_path_buffer) : NULL, path_buffer_size - (absolute ? descriptor->mount->path_length : 0), out_length);

	if (absolute) {
		*out_length += descriptor->mount->path_length;

		if (status == ferr_ok) {
			simple_memcpy(out_path_buffer, descriptor->mount->path, descriptor->mount->path_length);
		}
	}

	return status;
};

ferr_t vfsman_copy_info(vfsman_descriptor_t* obj, vfsman_node_info_t* out_info) {
	vfsman_descriptor_object_t* descriptor = (void*)obj;

	if (!descriptor || !out_info) {
		return ferr_invalid_argument;
	}

	if (!descriptor->mount->backend->copy_info) {
		return ferr_unsupported;
	}

	return descriptor->mount->backend->copy_info(descriptor->mount->context, obj, out_info);
};

ferr_t vfsman_open_rn(vfsman_descriptor_t* base_descriptor, const char* path, size_t path_length, vfsman_descriptor_flags_t flags, vfsman_descriptor_t** out_descriptor) {
	size_t base_len = 0;
	char* abs_path = NULL;
	vfsman_node_info_t base_info;

	if (!path || path_length == 0) {
		return ferr_invalid_argument;
	}

	if (sys_path_is_absolute_n(path, path_length)) {
		return vfsman_open_n(path, path_length, flags, out_descriptor);
	}

	if (!out_descriptor || !base_descriptor) {
		return ferr_invalid_argument;
	}

	if (vfsman_copy_info(base_descriptor, &base_info) == ferr_unsupported) {
		return ferr_unsupported;
	}

	if (base_info.type != vfsman_node_type_directory) {
		return ferr_invalid_argument;
	}

	if (vfsman_copy_path(base_descriptor, true, NULL, 0, &base_len) == ferr_unsupported) {
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
	if (sys_mempool_allocate(base_len + path_length + 1, NULL, (void*)&abs_path) != ferr_ok) {
		return ferr_temporary_outage;
	}

	if (vfsman_copy_path(base_descriptor, true, abs_path, base_len, &base_len) != ferr_ok) {
		LIBVFS_WUR_IGNORE(sys_mempool_free(abs_path));
		return ferr_temporary_outage;
	}

	char* abs_path_end = abs_path + base_len;

	sys_path_component_t component;
	for (ferr_t status = sys_path_component_first_n(path, path_length, &component); status == ferr_ok; status = sys_path_component_next(&component)) {
		if (component.length == 1 && component.component[0] == '.') {
			// ignore this component
			continue;
		}

		if (component.length == 2 && component.component[0] == '.' && component.component[1] == '.') {
			char* new_end = simple_strrnchr(abs_path, '/', abs_path_end - abs_path);

			if (!new_end) {
				new_end = abs_path;
			}

			abs_path_end = new_end;
			continue;
		}

		abs_path_end[0] = '/';
		++abs_path_end;

		simple_memcpy(abs_path_end, component.component, component.length);
		abs_path_end += component.length;
	}

	ferr_t status = vfsman_open_n(abs_path, abs_path_end - abs_path, flags, out_descriptor);

	LIBVFS_WUR_IGNORE(sys_mempool_free(abs_path));

	return status;
};

ferr_t vfsman_open_r(vfsman_descriptor_t* base_descriptor, const char* path, vfsman_descriptor_flags_t flags, vfsman_descriptor_t** out_descriptor) {
	return vfsman_open_rn(base_descriptor, path, path ? simple_strlen(path) : 0, flags, out_descriptor);
};

ferr_t vfsman_read(vfsman_descriptor_t* obj, size_t offset, void* buffer, size_t buffer_size, size_t* out_read_count) {
	vfsman_descriptor_object_t* descriptor = (void*)obj;

	if (!descriptor) {
		return ferr_invalid_argument;
	}

	if (!descriptor->mount->backend->read) {
		return ferr_unsupported;
	}

	return descriptor->mount->backend->read(descriptor->mount->context, obj, offset, buffer, buffer_size, out_read_count);
};

ferr_t vfsman_write(vfsman_descriptor_t* obj, size_t offset, const void* buffer, size_t buffer_size, size_t* out_written_count) {
	vfsman_descriptor_object_t* descriptor = (void*)obj;

	if (!descriptor) {
		return ferr_invalid_argument;
	}

	if (!descriptor->mount->backend->write) {
		return ferr_unsupported;
	}

	return descriptor->mount->backend->write(descriptor->mount->context, obj, offset, buffer, buffer_size, out_written_count);
};
