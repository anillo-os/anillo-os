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

#include <ferro/userspace/entry.h>
#include <ferro/core/paging.h>
#include <ferro/userspace/threads.h>
#include <ferro/core/panic.h>
#include <libsimple/libsimple.h>
#include <ferro/core/scheduler.h>
#include <ferro/userspace/processes.h>
#include <ferro/syscalls/syscalls.h>
#include <ferro/core/console.h>
#include <ferro/userspace/process-registry.h>
#include <ferro/core/ramdisk.h>

extern const fproc_descriptor_class_t fsyscall_shared_page_class;

void ferro_userspace_entry(void) {
	fpage_mapping_t* ramdisk_mapping = NULL;
	ferro_ramdisk_t* ramdisk = NULL;
	void* ramdisk_phys = NULL;
	size_t ramdisk_size = 0;
	fproc_did_t ramdisk_did = FPROC_DID_MAX;
	void* ramdisk_copy_tmp = NULL;

	futhread_init();

	fsyscall_init();

	fprocreg_init();

	fconsole_log("Loading init process...\n");

	ferro_ramdisk_get_data(&ramdisk, &ramdisk_phys, &ramdisk_size);
	fpanic_status(fpage_mapping_new(fpage_round_up_to_page_count(ramdisk_size), fpage_mapping_flag_zero, &ramdisk_mapping));

	// FIXME: the ramdisk needs to be loaded into its own set of pages so that we can bind the physical memory instead and avoid copying it
	fpanic_status(fpage_space_insert_mapping(fpage_space_current(), ramdisk_mapping, 0, fpage_round_up_to_page_count(ramdisk_size), 0, NULL, fpage_flag_zero, &ramdisk_copy_tmp));
	simple_memcpy(ramdisk_copy_tmp, ramdisk, ramdisk_size);
	fpanic_status(fpage_space_remove_mapping(fpage_space_current(), ramdisk_copy_tmp));

	fvfs_descriptor_t* vfsman_desc = NULL;
	fpanic_status(fvfs_open("/sys/vfsman/vfsman", fvfs_descriptor_flag_read | fvfs_descriptor_flags_execute, &vfsman_desc));

	fproc_t* proc = NULL;
	fpanic_status(fproc_new(vfsman_desc, NULL, &proc));

	fpanic_status(fprocreg_register(proc));

	fpanic_status(fproc_install_descriptor(proc, ramdisk_mapping, &fsyscall_shared_page_class, &ramdisk_did));

	if (ramdisk_did != 0) {
		// the ramdisk mapping DID *has* to be the first in the process
		fpanic("Wrong DID for ramdisk mapping");
	}

	fpage_mapping_release(ramdisk_mapping);

	fpanic_status(fproc_resume(proc));

	fvfs_release(vfsman_desc);
	fproc_release(proc);
};
