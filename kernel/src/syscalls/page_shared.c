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

#include <gen/ferro/userspace/syscall-handlers.h>
#include <ferro/userspace/processes.h>
#include <ferro/core/paging.h>
#include <libsimple/libsimple.h>

const fproc_descriptor_class_t fsyscall_shared_page_class = {
	.retain = (void*)fpage_mapping_retain,
	.release = (void*)fpage_mapping_release,
};

ferr_t fsyscall_handler_page_allocate_shared(uint64_t page_count, fsyscall_page_allocate_shared_flags_t flags, uint64_t* out_mapping_id) {
	ferr_t status = ferr_ok;
	fpage_mapping_t* mapping = NULL;
	fproc_did_t mapping_id = FPROC_DID_MAX;

	status = fpage_mapping_new(page_count, fpage_mapping_flag_zero, &mapping);
	if (status != ferr_ok) {
		goto out;
	}

	status = fproc_install_descriptor(fproc_current(), mapping, &fsyscall_shared_page_class, &mapping_id);
	if (status != ferr_ok) {
		goto out;
	}

out:
	if (status == ferr_ok) {
		*out_mapping_id = mapping_id;
	} else {
		if (mapping) {
			fpage_mapping_release(mapping);
		}
	}
	return status;
};

ferr_t fsyscall_handler_page_map_shared(uint64_t mapping_id, uint64_t page_count, uint64_t page_offset_count, fsyscall_page_map_shared_flags_t flags, uint8_t alignment_power, void* out_address) {
	ferr_t status = ferr_ok;
	const fproc_descriptor_class_t* desc_class = NULL;
	fpage_mapping_t* mapping = NULL;
	void* address = NULL;
	bool remove_mapping_on_fail = false;

	status = fproc_lookup_descriptor(fproc_current(), mapping_id, true, (void*)&mapping, &desc_class);
	if (status != ferr_ok) {
		goto out;
	}

	if (desc_class != &fsyscall_shared_page_class) {
		status = ferr_invalid_argument;
		goto out;
	}

	status = fpage_space_insert_mapping(fpage_space_current(), mapping, page_offset_count, page_count, alignment_power, fpage_flag_unprivileged, &address);
	if (status != ferr_ok) {
		goto out;
	}

	remove_mapping_on_fail = true;

	status = fproc_register_mapping(fproc_current(), address, page_count, 0, mapping);

out:
	if (mapping) {
		desc_class->release(mapping);
	}
	if (status == ferr_ok) {
		*(void**)out_address = address;
	} else {
		if (remove_mapping_on_fail) {
			FERRO_WUR_IGNORE(fpage_space_remove_mapping(fpage_space_current(), address));
		}
	}
	return status;
};

ferr_t fsyscall_handler_page_close_shared(uint64_t mapping_id) {
	ferr_t status = ferr_ok;
	const fproc_descriptor_class_t* desc_class = NULL;
	fpage_mapping_t* mapping = NULL;

	status = fproc_lookup_descriptor(fproc_current(), mapping_id, true, (void*)&mapping, &desc_class);
	if (status != ferr_ok) {
		goto out;
	}

	if (desc_class != &fsyscall_shared_page_class) {
		status = ferr_invalid_argument;
		goto out;
	}

	FERRO_WUR_IGNORE(fproc_uninstall_descriptor(fproc_current(), mapping_id));

out:
	if (mapping) {
		desc_class->release(mapping);
	}
	return status;
};

ferr_t fsyscall_handler_page_bind_shared(uint64_t mapping_id, uint64_t page_count, uint64_t page_offset_count, void* address) {
	ferr_t status = ferr_ok;
	size_t mapped_page_count;
	fproc_mapping_flags_t mapping_flags;
	fpage_mapping_t* old_mapping;

	const fproc_descriptor_class_t* desc_class = NULL;
	fpage_mapping_t* mapping = NULL;

	status = fproc_lookup_descriptor(fproc_current(), mapping_id, true, (void*)&mapping, &desc_class);
	if (status != ferr_ok) {
		goto out;
	}

	status = fproc_lookup_mapping(fproc_current(), address, &mapped_page_count, &mapping_flags, &old_mapping);
	if (status != ferr_ok) {
		goto out;
	}

	if (old_mapping) {
		size_t old_page_offset = 0;

		status = fpage_space_lookup_mapping(fpage_space_current(), address, false, NULL, &old_page_offset, NULL);
		if (status != ferr_ok) {
			goto out;
		}

		status = fpage_mapping_bind_indirect(mapping, page_offset_count, page_count, old_mapping, old_page_offset, 0);
	} else {
		status = fproc_unregister_mapping(fproc_current(), address, &mapped_page_count, &mapping_flags, &old_mapping);
		if (status != ferr_ok) {
			goto out;
		}

		status = fpage_space_move_into_mapping(fpage_space_current(), address, page_count, page_offset_count, mapping);
		if (status != ferr_ok) {
			goto out;
		}

		status = fproc_register_mapping(fproc_current(), address, page_count, 0, mapping);
	}

out:
	if (mapping) {
		desc_class->release(mapping);
	}
	if (old_mapping) {
		fpage_mapping_release(old_mapping);
	}
	return status;
};
