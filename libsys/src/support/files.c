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

#include <libsys/files.private.h>
#include <libsys/mempool.h>
#include <libsys/abort.h>
#include <stdbool.h>
#include <gen/libsyscall/syscall-wrappers.h>
#include <libsys/objects.private.h>
#include <libsimple/libsimple.h>
#include <libvfs/libvfs.private.h>
#include <libsys/channels.private.h>

#ifdef BUILDING_DYMPLE
static sys_channel_object_t proc_binary_channel = {
	.object = {
		.flags = 0,
		.object_class = &__sys_object_class_channel,
		.reference_count = UINT32_MAX,
	},

	// DID 0 is always the process binary channel
	.channel_did = 0,
};

static uint8_t proc_binary_channel_used = 0;
#elif !defined(BUILDING_STATIC)
#include <dymple/dymple.h>
#endif

static void sys_file_destroy(sys_object_t* object);

static const sys_object_class_t file_class = {
	LIBSYS_OBJECT_CLASS_INTERFACE(NULL),
	.destroy = sys_file_destroy,
};

LIBSYS_OBJECT_CLASS_GETTER(file, file_class);

static void sys_file_destroy(sys_object_t* object) {
	sys_file_object_t* file = (void*)object;

	if (file->file) {
		vfs_release(file->file);
	}

	sys_object_destroy(object);
};

ferr_t sys_file_open_special(sys_file_special_id_t id, sys_file_t** out_file) {
	ferr_t status = ferr_ok;
	sys_file_t* xfile = NULL;
	sys_file_object_t* file = NULL;

	status = sys_object_new(&file_class, sizeof(sys_file_object_t) - sizeof(sys_object_t), &xfile);
	if (status != ferr_ok) {
		goto out;
	}
	file = (void*)xfile;

	file->file = NULL;

	switch (id) {
		case sys_file_special_id_process_binary: {
#ifdef BUILDING_DYMPLE
			if (__atomic_test_and_set(&proc_binary_channel_used, __ATOMIC_RELAXED)) {
				status = ferr_permanent_outage;
				goto out;
			}

			status = vfs_open_raw((void*)&proc_binary_channel, &file->file);
			if (status != ferr_ok) {
				goto out;
			}
#elif !defined(BUILDING_STATIC)
			sys_channel_t* channel = NULL;

			status = dymple_open_process_binary_raw(&channel);
			if (status != ferr_ok) {
				goto out;
			}

			status = vfs_open_raw(channel, &file->file);
			if (status != ferr_ok) {
				sys_release(channel);
				goto out;
			}
#else
			status = ferr_unsupported;
#endif
		} break;

		default:
			status = ferr_invalid_argument;
			break;
	}

out:
	if (status == ferr_ok) {
		*out_file = xfile;
	} else {
		if (xfile) {
			sys_release(xfile);
		}
	}
	return status;
};

ferr_t sys_file_read(sys_file_t* obj, uint64_t offset, size_t buffer_size, void* out_buffer, size_t* out_read_count) {
	sys_file_object_t* file = (void*)obj;
	return vfs_file_read(file->file, offset, buffer_size, out_buffer, out_read_count);
};

ferr_t sys_file_read_data(sys_file_t* obj, uint64_t offset, size_t size, sys_data_t** out_data) {
	sys_file_object_t* file = (void*)obj;
	return vfs_file_read_data(file->file, offset, size, out_data);
};

#define OUTAGE_LIMIT 5

ferr_t sys_file_read_retry(sys_file_t* file, uint64_t offset, size_t buffer_size, void* out_buffer, size_t* out_read_count) {
	ferr_t status = ferr_ok;
	char* buffer_offset = out_buffer;
	size_t total_read_count = 0;
	size_t current_read_count = 0;
	size_t outages = 0;

	while (total_read_count < buffer_size) {
		status = sys_file_read(file, offset + total_read_count, buffer_size - total_read_count, buffer_offset, &current_read_count);
		if (status != ferr_ok) {
			switch (status) {
				case ferr_permanent_outage:
				case ferr_unsupported:
					status = ferr_invalid_argument;
					break;
				case ferr_temporary_outage:
					if (outages < OUTAGE_LIMIT) {
						// try again
						status = ferr_ok;
						++outages;
						continue;
					}
					// otherwise, we've reached the attempt limit on temporary outages.
					// stop here and report failure.
					break;
				default:
					break;
			}

			break;
		} else {
			// this call succeeded, so any previous streak of outages has been broken.
			outages = 0;
		}

		total_read_count += current_read_count;
		buffer_offset += current_read_count;
		current_read_count = 0;
	}

	if (out_read_count) {
		*out_read_count = total_read_count;
	}

	return status;
};

ferr_t sys_file_read_into_shared_data(sys_file_t* obj, uint64_t read_offset, uint64_t shared_data_offset, size_t size, sys_data_t* shared_data, size_t* out_read_count) {
	sys_file_object_t* file = (void*)obj;
	return vfs_file_read_into_shared_data(file->file, read_offset, size, shared_data, shared_data_offset, out_read_count);
};

ferr_t sys_file_write(sys_file_t* obj, uint64_t offset, size_t buffer_size, const void* buffer, size_t* out_written_count) {
	sys_file_object_t* file = (void*)obj;
	return vfs_file_write(file->file, offset, buffer_size, buffer, out_written_count);
};

ferr_t sys_file_copy_path(sys_file_t* obj, size_t buffer_size, void* out_buffer, size_t* out_actual_size) {
	sys_file_object_t* file = (void*)obj;
	return vfs_file_copy_path(file->file, out_buffer, buffer_size, out_actual_size);
};

ferr_t sys_file_copy_path_allocate(sys_file_t* file, char** out_string, size_t* out_string_length) {
	ferr_t status = ferr_ok;
	size_t required_size = 0;
	void* buffer = NULL;

	if (!out_string) {
		return ferr_invalid_argument;
	}

	status = sys_file_copy_path(file, 0, NULL, &required_size);

	if (status != ferr_too_big) {
		// that's weird.
		return ferr_unknown;
	}

	while (true) {
		status = sys_mempool_reallocate(buffer, required_size, NULL, &buffer);
		if (status != ferr_ok) {
			sys_abort_status(sys_mempool_free(buffer));
			return ferr_temporary_outage;
		}

		status = sys_file_copy_path(file, required_size, buffer, &required_size);
		if (status == ferr_too_big) {
			continue;
		} else if (status != ferr_ok) {
			sys_abort_status(sys_mempool_free(buffer));
			return status;
		}

		break;
	}

	*out_string = buffer;

	if (out_string_length) {
		*out_string_length = required_size;
	}

	return status;
};

ferr_t sys_file_open(const char* path, sys_file_t** out_file) {
	if (!path) {
		return ferr_invalid_argument;
	}

	return sys_file_open_n(path, simple_strlen(path), out_file);
};

ferr_t sys_file_open_n(const char* path, size_t path_length, sys_file_t** out_file) {
	ferr_t status = ferr_ok;
	sys_file_t* xfile = NULL;
	sys_file_object_t* file = NULL;

	status = sys_object_new(&file_class, sizeof(sys_file_object_t) - sizeof(sys_object_t), &xfile);
	if (status != ferr_ok) {
		goto out;
	}
	file = (void*)xfile;

	file->file = NULL;

	status = vfs_open_n(path, path_length, &file->file);

out:
	if (status == ferr_ok) {
		*out_file = xfile;
	} else {
		if (xfile) {
			sys_release(xfile);
		}
	}
	return status;
};
