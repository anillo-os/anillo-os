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

#include <libsys/vfs.private.h>
#include <libsys/mempool.h>
#include <libsys/abort.h>
#include <stdbool.h>
#include <gen/libsyscall/syscall-wrappers.h>
#include <libsys/objects.private.h>
#include <libsimple/libsimple.h>
#include <libvfs/libvfs.private.h>
#include <libsys/channels.private.h>

#if BUILDING_DYMPLE
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
#elif !BUILDING_STATIC
#include <dymple/dymple.h>
#endif

ferr_t vfs_open_special(vfs_node_special_id_t id, vfs_node_t** out_node) {
	ferr_t status = ferr_ok;
	vfs_node_t* node = NULL;

	switch (id) {
		case vfs_node_special_id_process_binary: {
#if BUILDING_DYMPLE
			if (__atomic_test_and_set(&proc_binary_channel_used, __ATOMIC_RELAXED)) {
				status = ferr_permanent_outage;
				goto out;
			}

			status = vfs_open_raw((void*)&proc_binary_channel, &node);
			if (status != ferr_ok) {
				goto out;
			}
#elif !BUILDING_STATIC
			sys_channel_t* channel = NULL;

			status = dymple_open_process_binary_raw(&channel);
			if (status != ferr_ok) {
				goto out;
			}

			status = vfs_open_raw(channel, &node);
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
		*out_node = node;
	} else {
		if (node) {
			sys_release(node);
		}
	}
	return status;
};

#define OUTAGE_LIMIT 5

ferr_t vfs_node_read_retry(vfs_node_t* node, uint64_t offset, size_t buffer_size, void* out_buffer, size_t* out_read_count) {
	ferr_t status = ferr_ok;
	char* buffer_offset = out_buffer;
	size_t total_read_count = 0;
	size_t current_read_count = 0;
	size_t outages = 0;

	while (total_read_count < buffer_size) {
		status = vfs_node_read(node, offset + total_read_count, buffer_size - total_read_count, buffer_offset, &current_read_count);
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

ferr_t vfs_node_copy_path_allocate(vfs_node_t* node, char** out_string, size_t* out_string_length) {
	ferr_t status = ferr_ok;
	size_t required_size = 0;
	void* buffer = NULL;

	if (!out_string) {
		return ferr_invalid_argument;
	}

	status = vfs_node_copy_path(node, 0, NULL, &required_size);

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

		status = vfs_node_copy_path(node, required_size, buffer, &required_size);
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
