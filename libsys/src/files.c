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

#include <libsys/files.h>
#include <libsys/mempool.h>
#include <libsys/abort.h>
#include <stdbool.h>
#include <gen/libsyscall/syscall-wrappers.h>
#include <libsys/objects.private.h>
#include <libsimple/libsimple.h>

LIBSYS_STRUCT(sys_file_object) {
	sys_object_t object;
	sys_fd_t fd;
};

static void sys_file_destroy(sys_object_t* object);

static const sys_object_class_t file_class = {
	.destroy = sys_file_destroy,
};

static void sys_file_destroy(sys_object_t* object) {
	sys_file_object_t* file = (void*)object;

	if (file->fd) {
		sys_abort_status(libsyscall_wrapper_fd_close(file->fd));
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

	file->fd = SYS_FD_INVALID;

	status = sys_file_open_special_fd(id, &file->fd);
	if (status != ferr_ok) {
		goto out;
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

ferr_t sys_file_open_special_fd(sys_file_special_id_t id, sys_fd_t* out_fd) {
	ferr_t status = ferr_ok;

	switch (id) {
		case sys_file_special_id_process_binary:
			status = libsyscall_wrapper_fd_open_special(0, out_fd);
			break;
		default:
			status = ferr_invalid_argument;
			break;
	}

out:
	return status;
};

ferr_t sys_file_close_fd(sys_fd_t fd) {
	return libsyscall_wrapper_fd_close(fd);
};

ferr_t sys_file_fd(sys_file_t* xfile, sys_fd_t* out_fd) {
	sys_file_object_t* file = (void*)xfile;
	ferr_t status = ferr_ok;

	if (!xfile) {
		status = ferr_invalid_argument;
		goto out;
	}

	if (out_fd) {
		*out_fd = file->fd;
	}

out:
	return status;
};

ferr_t sys_file_from_fd(sys_fd_t fd, sys_file_t** out_file) {
	ferr_t status = ferr_ok;
	sys_file_t* xfile = NULL;
	sys_file_object_t* file = NULL;

	if (!out_file || fd == SYS_FD_INVALID) {
		status = ferr_invalid_argument;
		goto out;
	}

	status = sys_object_new(&file_class, sizeof(sys_file_object_t) - sizeof(sys_object_t), &xfile);
	if (status != ferr_ok) {
		goto out;
	}
	file = (void*)xfile;

	file->fd = fd;

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

ferr_t sys_file_read(sys_file_t* xfile, uint64_t offset, size_t buffer_size, void* out_buffer, size_t* out_read_count) {
	ferr_t status = ferr_ok;
	sys_fd_t fd = SYS_FD_INVALID;

	status = sys_file_fd(xfile, &fd);
	if (status != ferr_ok) {
		goto out;
	}

	status = sys_file_read_fd(fd, offset, buffer_size, out_buffer, out_read_count);

out:
	return status;
};

ferr_t sys_file_read_fd(sys_fd_t fd, uint64_t offset, size_t buffer_size, void* out_buffer, size_t* out_read_count) {
	return libsyscall_wrapper_fd_read(fd, offset, buffer_size, out_buffer, out_read_count);
};

ferr_t sys_file_read_retry(sys_file_t* xfile, uint64_t offset, size_t buffer_size, void* out_buffer, size_t* out_read_count) {
	ferr_t status = ferr_ok;
	sys_fd_t fd = SYS_FD_INVALID;

	status = sys_file_fd(xfile, &fd);
	if (status != ferr_ok) {
		goto out;
	}

	status = sys_file_read_retry_fd(fd, offset, buffer_size, out_buffer, out_read_count);

out:
	return status;
};

#define OUTAGE_LIMIT 5

ferr_t sys_file_read_retry_fd(sys_fd_t fd, uint64_t offset, size_t buffer_size, void* out_buffer, size_t* out_read_count) {
	ferr_t status = ferr_ok;
	char* buffer_offset = out_buffer;
	size_t total_read_count = 0;
	size_t current_read_count = 0;
	size_t outages = 0;

	while (total_read_count < buffer_size) {
		status = sys_file_read_fd(fd, offset + total_read_count, buffer_size - total_read_count, buffer_offset, &current_read_count);
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

ferr_t sys_file_write(sys_file_t* xfile, uint64_t offset, size_t buffer_size, const void* buffer, size_t* out_written_count) {
	ferr_t status = ferr_ok;
	sys_fd_t fd = SYS_FD_INVALID;

	status = sys_file_fd(xfile, &fd);
	if (status != ferr_ok) {
		goto out;
	}

	status = sys_file_write_fd(fd, offset, buffer_size, buffer, out_written_count);

out:
	return status;
};

ferr_t sys_file_write_fd(sys_fd_t fd, uint64_t offset, size_t buffer_size, const void* buffer, size_t* out_written_count) {
	return libsyscall_wrapper_fd_write(fd, offset, buffer_size, buffer, out_written_count);
};


ferr_t sys_file_copy_path(sys_file_t* xfile, size_t buffer_size, void* out_buffer, size_t* out_actual_size) {
	ferr_t status = ferr_ok;
	sys_fd_t fd = SYS_FD_INVALID;

	status = sys_file_fd(xfile, &fd);
	if (status != ferr_ok) {
		goto out;
	}

	status = sys_file_copy_path_fd(fd, buffer_size, out_buffer, out_actual_size);

out:
	return status;
};

ferr_t sys_file_copy_path_fd(sys_fd_t fd, size_t buffer_size, void* out_buffer, size_t* out_actual_size) {
	return libsyscall_wrapper_fd_copy_path(fd, buffer_size, out_buffer, out_actual_size);
};

ferr_t sys_file_copy_path_allocate(sys_file_t* xfile, char** out_string, size_t* out_string_length) {
	ferr_t status = ferr_ok;
	sys_fd_t fd = SYS_FD_INVALID;

	status = sys_file_fd(xfile, &fd);
	if (status != ferr_ok) {
		goto out;
	}

	status = sys_file_copy_path_allocate_fd(fd, out_string, out_string_length);

out:
	return status;
};

ferr_t sys_file_copy_path_allocate_fd(sys_fd_t fd, char** out_string, size_t* out_string_length) {
	ferr_t status = ferr_ok;
	size_t required_size = 0;
	void* buffer = NULL;

	if (!out_string) {
		return ferr_invalid_argument;
	}

	status = sys_file_copy_path_fd(fd, 0, NULL, &required_size);

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

		status = sys_file_copy_path_fd(fd, required_size, buffer, &required_size);
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

ferr_t sys_file_open_fd(const char* path, sys_fd_t* out_fd) {
	if (!path) {
		return ferr_invalid_argument;
	}

	return sys_file_open_fd_n(path, simple_strlen(path), out_fd);
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

	file->fd = SYS_FD_INVALID;

	status = sys_file_open_fd_n(path, path_length, &file->fd);
	if (status != ferr_ok) {
		goto out;
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

ferr_t sys_file_open_fd_n(const char* path, size_t path_length, sys_fd_t* out_fd) {
	return libsyscall_wrapper_fd_open(path, path_length, 0, out_fd);
};
