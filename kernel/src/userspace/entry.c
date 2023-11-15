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
#include <ferro/drivers/pci.private.h>
#include <ferro/syscalls/channels.private.h>
#include <ferro/core/mempool.h>

extern const fproc_descriptor_class_t fsyscall_shared_page_class;

static void ferro_userspace_handoff_thread(void* data) {
	fchannel_t* channel = data;
	fchannel_message_t incoming_message;
	fchannel_message_t outgoing_message;
	fchannel_message_attachment_mapping_t* attachment = NULL;
	fpage_mapping_t* mapping = NULL;
	ferro_fb_info_t* body = NULL;

	if (fchannel_receive(channel, 0, &incoming_message) != ferr_ok) {
		// assume the other side was closed
		goto die;
	}

	// we've received the go-ahead to start the handoff
	simple_memset(&outgoing_message, 0, sizeof(outgoing_message));
	outgoing_message.conversation_id = incoming_message.conversation_id;

	fpanic_status(fmempool_allocate(sizeof(*body), NULL, (void*)&body));
	fpanic_status(fmempool_allocate(sizeof(*body), NULL, (void*)&attachment));

	simple_memset(body, 0, sizeof(*body));
	outgoing_message.body = body;
	outgoing_message.body_length = sizeof(*body);

	if (ferro_fb_get_info()) {
		simple_memcpy(body, ferro_fb_get_info(), sizeof(*body));
		body->base = NULL;
	}

	// we don't need the incoming messaage anymore
	fchannel_message_destroy(&incoming_message);

	simple_memset(attachment, 0, sizeof(*attachment));
	attachment->header.length = sizeof(*attachment);
	attachment->header.next_offset = 0;
	attachment->header.type = fchannel_message_attachment_type_mapping;

	if (ferro_fb_handoff(&attachment->mapping) == ferr_ok) {
		outgoing_message.attachments = (void*)attachment;
		outgoing_message.attachments_length = sizeof(*attachment);
	} else {
		FERRO_WUR_IGNORE(fmempool_free(attachment));
	}

	if (fchannel_send(channel, 0, &outgoing_message) != ferr_ok) {
		fchannel_message_destroy(&outgoing_message);
	}

die:
	FERRO_WUR_IGNORE(fchannel_close(channel));
	fchannel_release(channel);
	fthread_kill_self();
};

void ferro_userspace_entry(void) {
	fpage_mapping_t* ramdisk_mapping = NULL;
	ferro_ramdisk_t* ramdisk = NULL;
	void* ramdisk_phys = NULL;
	size_t ramdisk_size = 0;
	fproc_did_t ramdisk_did = FPROC_DID_MAX;
	void* ramdisk_copy_tmp = NULL;
	fproc_did_t pciman_did = FPROC_DID_MAX;
	fchannel_t* handoff_our_side = NULL;
	fchannel_t* handoff_their_side = NULL;
	fthread_t* handoff_manager_thread = NULL;
	fproc_did_t handoff_did = FPROC_DID_MAX;

	fpanic_status(fchannel_new_pair(&handoff_our_side, &handoff_their_side));
	fpanic_status(fthread_new(ferro_userspace_handoff_thread, handoff_our_side, NULL, 2ull * 1024 * 1024, 0, &handoff_manager_thread));
	fpanic_status(fsched_manage(handoff_manager_thread));
	fpanic_status(fthread_resume(handoff_manager_thread));

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

	fvfs_descriptor_t* sysman_desc = NULL;
	fpanic_status(fvfs_open("/sys/sysman/sysman", fvfs_descriptor_flag_read | fvfs_descriptor_flags_execute, &sysman_desc));

	fproc_t* proc = NULL;
	fpanic_status(fproc_new(sysman_desc, NULL, &proc));

	fpanic_status(fprocreg_register(proc));

	fpanic_status(fproc_install_descriptor(proc, ramdisk_mapping, &fsyscall_shared_page_class, &ramdisk_did));

	if (ramdisk_did != 0) {
		// the ramdisk mapping DID *has* to be the first in the process
		fpanic("Wrong DID for ramdisk mapping");
	}

	fpage_mapping_release(ramdisk_mapping);

	// wait for pciman to start
	while (!fpci_pciman_client_channel) {
		farch_lock_spin_yield();
	}

	fpanic_status(fproc_install_descriptor(proc, fpci_pciman_client_channel, &fsyscall_channel_descriptor_class, &pciman_did));

	if (pciman_did != 1) {
		// the pciman DID *has* to be the second in the process
		fpanic("Wrong DID for pciman client channel");
	}

	fpanic_status(fproc_install_descriptor(proc, handoff_their_side, &fsyscall_channel_descriptor_class, &handoff_did));

	if (handoff_did != 2) {
		// the handoff DID *has* to be the third in the process
		fpanic("Wrong DID for framebuffer handoff client channel");
	}

	fchannel_release(handoff_their_side);

	fpanic_status(fproc_resume(proc));

	fvfs_release(sysman_desc);
	fproc_release(proc);
};
