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
#include <ferro/core/mempool.h>
#include <libsimple/libsimple.h>
#include <ferro/core/panic.h>
#include <ferro/userspace/uio.h>

// for doing locked receives and sends
#include <ferro/core/channels.private.h>

#include <ferro/syscalls/channels.private.h>

// TODO: actually support timeouts

void fsyscall_init_channels(void) {

};

const fproc_descriptor_class_t fsyscall_channel_descriptor_class = {
	.retain = (void*)fchannel_retain,
	.release = (void*)fchannel_release,
};

extern const fproc_descriptor_class_t fsyscall_shared_page_class;

ferr_t fsyscall_handler_channel_create_pair(uint64_t* out_channel_ids) {
	ferr_t status = ferr_ok;
	fchannel_t* channels[2] = {0};
	uint64_t descriptor_ids[2] = {FPROC_DID_MAX, FPROC_DID_MAX};

	if (!out_channel_ids) {
		status = ferr_invalid_argument;
		goto out;
	}

	status = fchannel_new_pair(&channels[0], &channels[1]);
	if (status != ferr_ok) {
		goto out;
	}

	status = fproc_install_descriptor(fproc_current(), channels[0], &fsyscall_channel_descriptor_class, &descriptor_ids[0]);
	if (status != ferr_ok) {
		goto out;
	}

	status = fproc_install_descriptor(fproc_current(), channels[1], &fsyscall_channel_descriptor_class, &descriptor_ids[1]);
	if (status != ferr_ok) {
		goto out;
	}

	status = ferro_uio_copy_out(descriptor_ids, sizeof(descriptor_ids), (uintptr_t)out_channel_ids);

out:
	if (channels[0]) {
		fchannel_release(channels[0]);
	}
	if (channels[1]) {
		fchannel_release(channels[1]);
	}
	if (status != ferr_ok) {
		if (descriptor_ids[0] != FPROC_DID_MAX) {
			FERRO_WUR_IGNORE(fproc_uninstall_descriptor(fproc_current(), descriptor_ids[0]));
		}
		if (descriptor_ids[1] != FPROC_DID_MAX) {
			FERRO_WUR_IGNORE(fproc_uninstall_descriptor(fproc_current(), descriptor_ids[1]));
		}
	}
	return status;
};

ferr_t fsyscall_handler_channel_conversation_create(uint64_t channel_id, fchannel_conversation_id_t* out_conversation_id) {
	ferr_t status = ferr_ok;
	fchannel_t* channel = NULL;
	const fproc_descriptor_class_t* desc_class = NULL;
	fchannel_conversation_id_t convo_id = fchannel_conversation_id_none;

	if (!out_conversation_id) {
		status = ferr_invalid_argument;
		goto out;
	}

	status = fproc_lookup_descriptor(fproc_current(), channel_id, true, (void*)&channel, &desc_class);
	if (status != ferr_ok) {
		goto out;
	}

	if (desc_class != &fsyscall_channel_descriptor_class) {
		status = ferr_invalid_argument;
		goto out;
	}

	convo_id = fchannel_next_conversation_id(channel);

	status = ferro_uio_copy_out(&convo_id, sizeof(convo_id), (uintptr_t)out_conversation_id);

out:
	if (channel) {
		desc_class->release(channel);
	}
	return status;
};

// !!! IMPORTANT !!!
//
// this operation must remain atomic as part of a contract with userspace.
// if the message cannot be sent, its contents must not be modified or invalidated in any observable way.
ferr_t fsyscall_handler_channel_send(uint64_t channel_id, fchannel_send_flags_t flags, uint64_t timeout, fsyscall_timeout_type_t timeout_type, fsyscall_channel_message_t* user_in_out_message) {
	ferr_t status = ferr_ok;
	fchannel_t* channel = NULL;
	const fproc_descriptor_class_t* desc_class = NULL;
	fchannel_message_t message;
	size_t kernel_attachments_length = 0;
	size_t initialized_attachments = 0;
	fchannel_message_attachment_header_t* kernel_attachment_header = NULL;
	fchannel_message_attachment_header_t* previous_kernel_attachment_header = NULL;
	fchannel_send_lock_state_t send_lock_state;
	fsyscall_channel_message_t in_message;
	void* copied_attachments = NULL;

	simple_memset(&message, 0, sizeof(message));

	status = ferro_uio_copy_in_noalloc((uintptr_t)user_in_out_message, sizeof(in_message), &in_message);
	if (status != ferr_ok) {
		goto out;
	}

	if (in_message.attachments_length > 0) {
		status = ferro_uio_copy_in(in_message.attachments_address, in_message.attachments_length, &copied_attachments);
		if (status != ferr_ok) {
			goto out;
		}

		in_message.attachments_address = (uintptr_t)copied_attachments;
	}

	for (const fsyscall_channel_message_attachment_header_t* header = copied_attachments; header != NULL && (uintptr_t)header - (uintptr_t)copied_attachments < in_message.attachments_length; header = (header->next_offset == 0) ? NULL : (const void*)((const char*)header + header->next_offset)) {
		switch (header->type) {
			case fchannel_message_attachment_type_channel: {
				kernel_attachments_length += sizeof(fchannel_message_attachment_channel_t);
			} break;

			case fchannel_message_attachment_type_null: {
				kernel_attachments_length += sizeof(fchannel_message_attachment_null_t);
			} break;

			case fchannel_message_attachment_type_mapping: {
				kernel_attachments_length += sizeof(fchannel_message_attachment_mapping_t);
			} break;

			case fchannel_message_attachment_type_data: {
				kernel_attachments_length += sizeof(fchannel_message_attachment_data_t);
			} break;

			default:
				status = ferr_invalid_argument;
				goto out;
		}
	}

	status = fproc_lookup_descriptor(fproc_current(), channel_id, true, (void*)&channel, &desc_class);
	if (status != ferr_ok) {
		goto out;
	}

	if (desc_class != &fsyscall_channel_descriptor_class) {
		status = ferr_invalid_argument;
		goto out;
	}

	message.conversation_id = in_message.conversation_id;
	message.body_length = in_message.body_length;
	message.attachments_length = kernel_attachments_length;

	if (message.body_length > 0) {
		status = fmempool_allocate(message.body_length, NULL, &message.body);
		if (status != ferr_ok) {
			goto out;
		}

		status = ferro_uio_copy_in_noalloc(in_message.body_address, message.body_length, message.body);
		if (status != ferr_ok) {
			goto out;
		}
	}

	if (kernel_attachments_length > 0) {
		status = fmempool_allocate(kernel_attachments_length, NULL, (void*)&message.attachments);
		if (status != ferr_ok) {
			goto out;
		}

		simple_memset(message.attachments, 0, message.attachments_length);

		kernel_attachment_header = (void*)message.attachments;
		for (fsyscall_channel_message_attachment_header_t* header = copied_attachments; header != NULL && (uintptr_t)header - (uintptr_t)copied_attachments < in_message.attachments_length; (header = (header->next_offset == 0) ? NULL : (void*)((char*)header + header->next_offset)), (previous_kernel_attachment_header = kernel_attachment_header), (kernel_attachment_header = (void*)((char*)kernel_attachment_header + kernel_attachment_header->length))) {
			if (previous_kernel_attachment_header) {
				previous_kernel_attachment_header->next_offset = (uintptr_t)kernel_attachment_header - (uintptr_t)previous_kernel_attachment_header;
			}

			switch (header->type) {
				case fchannel_message_attachment_type_channel: {
					const fsyscall_channel_message_attachment_channel_t* syscall_channel_attachment = (const void*)header;
					fchannel_message_attachment_channel_t* channel_attachment = (void*)kernel_attachment_header;
					fchannel_t* attached_channel = NULL;
					const fproc_descriptor_class_t* desc_class = NULL;

					status = fproc_lookup_descriptor(fproc_current(), syscall_channel_attachment->channel_id, true, (void*)&attached_channel, &desc_class);
					if (status != ferr_ok) {
						status = ferr_invalid_argument;
						goto out;
					}

					if (desc_class != &fsyscall_channel_descriptor_class) {
						desc_class->release(attached_channel);
						status = ferr_invalid_argument;
						goto out;
					}

					channel_attachment->channel = attached_channel;
					channel_attachment->header.type = fchannel_message_attachment_type_channel;
					channel_attachment->header.length = sizeof(*channel_attachment);
				} break;

				case fchannel_message_attachment_type_mapping: {
					const fsyscall_channel_message_attachment_mapping_t* syscall_mapping_attachment = (const void*)header;
					fchannel_message_attachment_mapping_t* mapping_attachment = (void*)kernel_attachment_header;
					fpage_mapping_t* attached_mapping = NULL;
					const fproc_descriptor_class_t* desc_class = NULL;

					status = fproc_lookup_descriptor(fproc_current(), syscall_mapping_attachment->mapping_id, true, (void*)&attached_mapping, &desc_class);
					if (status != ferr_ok) {
						status = ferr_invalid_argument;
						goto out;
					}

					if (desc_class != &fsyscall_shared_page_class) {
						desc_class->release(attached_mapping);
						status = ferr_invalid_argument;
						goto out;
					}

					mapping_attachment->mapping = attached_mapping;
					mapping_attachment->header.type = fchannel_message_attachment_type_mapping;
					mapping_attachment->header.length = sizeof(*mapping_attachment);
				} break;

				case fchannel_message_attachment_type_data: {
					const fsyscall_channel_message_attachment_data_t* syscall_data_attachment = (const void*)header;
					fchannel_message_attachment_data_t* data_attachment = (void*)kernel_attachment_header;
					fpage_mapping_t* shared_data = NULL;
					void* copied_data;

					if (syscall_data_attachment->flags & fsyscall_channel_message_attachment_data_flag_shared) {
						const fproc_descriptor_class_t* desc_class = NULL;

						status = fproc_lookup_descriptor(fproc_current(), syscall_data_attachment->target, true, (void*)&shared_data, &desc_class);
						if (status != ferr_ok) {
							status = ferr_invalid_argument;
							goto out;
						}

						if (desc_class != &fsyscall_shared_page_class) {
							desc_class->release(shared_data);
							status = ferr_invalid_argument;
							goto out;
						}

						data_attachment->shared_data = shared_data;
						data_attachment->flags |= fchannel_message_attachment_data_flag_shared;
					} else {
						status = fmempool_allocate(syscall_data_attachment->length, NULL, &copied_data);
						if (status != ferr_ok) {
							status = ferr_temporary_outage;
							goto out;
						}

						status = ferro_uio_copy_in_noalloc(syscall_data_attachment->target, syscall_data_attachment->length, copied_data);
						if (status != ferr_ok) {
							goto out;
						}

						data_attachment->copied_data = copied_data;
					}

					data_attachment->length = syscall_data_attachment->length;
					data_attachment->header.type = fchannel_message_attachment_type_data;
					data_attachment->header.length = sizeof(*data_attachment);
				} break;

				case fchannel_message_attachment_type_null: {
					const fsyscall_channel_message_attachment_null_t* syscall_null_attachment = (const void*)header;
					fchannel_message_attachment_null_t* null_attachment = (void*)kernel_attachment_header;

					null_attachment->header.type = fchannel_message_attachment_type_null;
					null_attachment->header.length = sizeof(*null_attachment);
				} break;
			}

			++initialized_attachments;
		}
	}

	// now let's see if we can send the message
	status = fchannel_lock_send(channel, flags | fchannel_send_kernel_flag_interruptible, &send_lock_state);
	if (status != ferr_ok) {
		goto out;
	}

	// if we got here, we can definitely send the message.
	// we can now clean up resources from userspace because we know we can no longer fail.

	for (fsyscall_channel_message_attachment_header_t* header = copied_attachments; header != NULL && (uintptr_t)header - (uintptr_t)copied_attachments < in_message.attachments_length; header = (header->next_offset == 0) ? NULL : (void*)((char*)header + header->next_offset)) {
		switch (header->type) {
			case fchannel_message_attachment_type_channel: {
				const fsyscall_channel_message_attachment_channel_t* syscall_channel_attachment = (const void*)header;

				FERRO_WUR_IGNORE(fproc_uninstall_descriptor(fproc_current(), syscall_channel_attachment->channel_id));
			} break;

			// nothing to clean up here
			case fchannel_message_attachment_type_mapping:
				// mappings don't need to uninstall the mapping descriptor,
				// since it's perfectly valid for the mapping to be shared
				// (that's actually the primary reason for passing around mappings)
			case fchannel_message_attachment_type_data:
				// ditto for this; userspace is allowed to hold on to the
				// shared mapping or original data
			case fchannel_message_attachment_type_null:
				break;

			// this actually can't happen because we've already checked for this earlier
			default:
				fpanic("impossible error: bad message attachment type after locking channel for sending");
		}
	}

	fchannel_send_locked(channel, &message, &send_lock_state);

	fchannel_unlock_send(channel, &send_lock_state);

	status = ferro_uio_copy_out(&message.conversation_id, sizeof(message.conversation_id), (uintptr_t)&user_in_out_message->conversation_id);

out:
	if (channel) {
		desc_class->release(channel);
	}
	if (status != ferr_ok) {
		if (message.attachments) {
			fchannel_message_attachment_header_t* header = (void*)message.attachments;

			for (size_t i = 0; i < initialized_attachments && header != NULL; ++i) {
				switch (header->type) {
					case fchannel_message_attachment_type_channel: {
						fchannel_message_attachment_channel_t* channel_attachment = (void*)header;

						fchannel_release(channel_attachment->channel);
					} break;

					case fchannel_message_attachment_type_mapping: {
						fchannel_message_attachment_mapping_t* mapping_attachment = (void*)header;

						fpage_mapping_release(mapping_attachment->mapping);
					} break;

					case fchannel_message_attachment_type_data: {
						fchannel_message_attachment_data_t* data_attachment = (void*)header;

						if (data_attachment->flags & fchannel_message_attachment_data_flag_shared) {
							fpage_mapping_release(data_attachment->shared_data);
						} else {
							FERRO_WUR_IGNORE(fmempool_free(data_attachment->copied_data));
						}
					} break;

					case fchannel_message_attachment_type_null:
					default:
						// nothing to clean up here
						break;
				}

				header = (header->next_offset == 0) ? NULL : (void*)((char*)header + header->next_offset);
			}

			FERRO_WUR_IGNORE(fmempool_free(message.attachments));
		}
		if (message.body) {
			FERRO_WUR_IGNORE(fmempool_free(message.body));
		}
	}
	if (copied_attachments) {
		ferro_uio_copy_free(copied_attachments, in_message.attachments_length);
	}
	return status;
};

ferr_t fsyscall_handler_channel_receive(uint64_t channel_id, fsyscall_channel_receive_flags_t flags, uint64_t timeout, fsyscall_timeout_type_t timeout_type, fsyscall_channel_message_t* user_in_out_message) {
	ferr_t status = ferr_ok;
	fchannel_t* channel = NULL;
	const fproc_descriptor_class_t* desc_class = NULL;
	fchannel_message_t message;
	fchannel_receive_lock_state_t lock_state;
	size_t required_attachments_size = 0;
	size_t initialized_attachments = 0;
	fchannel_receive_flags_t kernel_flags = 0;
	fchannel_message_id_t target_id = fchannel_message_id_invalid;
	bool pre_receive_peek = (flags & fsyscall_channel_receive_flag_pre_receive_peek) != 0;
	void* syscall_attachments_buffer = NULL;
	fsyscall_channel_message_t in_message;

	status = ferro_uio_copy_in_noalloc((uintptr_t)user_in_out_message, sizeof(in_message), &in_message);
	if (status != ferr_ok) {
		goto out;
	}

	if ((flags & fsyscall_channel_receive_flag_match_message_id) != 0) {
		// we can only look for messages with matching message IDs if we're not going to wait for a message
		if ((flags & fsyscall_channel_receive_flag_no_wait) == 0) {
			status = ferr_invalid_argument;
			goto out;
		}

		target_id = in_message.message_id;
	}

	if ((flags & fsyscall_channel_receive_flag_no_wait) != 0) {
		kernel_flags |= fchannel_receive_flag_no_wait;
	}

	status = fproc_lookup_descriptor(fproc_current(), channel_id, true, (void*)&channel, &desc_class);
	if (status != ferr_ok) {
		goto out_unlocked;
	}

	if (desc_class != &fsyscall_channel_descriptor_class) {
		status = ferr_invalid_argument;
		goto out_unlocked;
	}

	status = fchannel_lock_receive(channel, kernel_flags | fchannel_receive_flag_interruptible, &lock_state);
	if (status != ferr_ok) {
		goto out_unlocked;
	}

	// peek the message
	fchannel_receive_locked(channel, true, &message, &lock_state);

	// if we want a specific message, check if this is the one we want
	if (target_id != fchannel_message_id_invalid && message.message_id != target_id) {
		status = ferr_resource_unavailable;
		goto out;
	}

	// first, check if we have enough space in the provided buffer to receive the message

	for (fchannel_message_attachment_header_t* kernel_attachment_header = (void*)message.attachments; kernel_attachment_header != NULL; kernel_attachment_header = (kernel_attachment_header->next_offset == 0) ? NULL : (void*)((char*)kernel_attachment_header + kernel_attachment_header->next_offset)) {
		switch (kernel_attachment_header->type) {
			case fchannel_message_attachment_type_channel: {
				required_attachments_size += sizeof(fsyscall_channel_message_attachment_channel_t);
			} break;

			case fchannel_message_attachment_type_mapping: {
				required_attachments_size += sizeof(fsyscall_channel_message_attachment_mapping_t);
			} break;

			case fchannel_message_attachment_type_data: {
				required_attachments_size += sizeof(fsyscall_channel_message_attachment_data_t);
			} break;

			case fchannel_message_attachment_type_null: {
				required_attachments_size += sizeof(fsyscall_channel_message_attachment_null_t);
			} break;
		}
	}

	if (in_message.attachments_length < required_attachments_size || in_message.body_length < message.body_length) {
		status = ferr_too_big;
		goto out;
	}

	status = fmempool_allocate(required_attachments_size, NULL, &syscall_attachments_buffer);
	if (status != ferr_ok) {
		goto out;
	}

	status = ferro_uio_copy_in_noalloc(in_message.attachments_address, required_attachments_size, syscall_attachments_buffer);
	if (status != ferr_ok) {
		goto out;
	}

	// now let's try to convert the message attachments to userspace format

	{
		fsyscall_channel_message_attachment_header_t* syscall_attachment_header = NULL;
		fsyscall_channel_message_attachment_header_t* previous_syscall_attachment_header = NULL;

		syscall_attachment_header = syscall_attachments_buffer;
		for (fchannel_message_attachment_header_t* kernel_attachment_header = (void*)message.attachments; kernel_attachment_header != NULL; (kernel_attachment_header = (kernel_attachment_header->next_offset == 0) ? NULL : (void*)((char*)kernel_attachment_header + kernel_attachment_header->next_offset)), (previous_syscall_attachment_header = syscall_attachment_header), (syscall_attachment_header = (void*)((char*)syscall_attachment_header + syscall_attachment_header->length))) {
			if (previous_syscall_attachment_header) {
				previous_syscall_attachment_header->next_offset = (uintptr_t)syscall_attachment_header - (uintptr_t)previous_syscall_attachment_header;
			}

			switch (kernel_attachment_header->type) {
				case fchannel_message_attachment_type_channel: {
					fchannel_message_attachment_channel_t* kernel_channel_attachment = (void*)kernel_attachment_header;
					fsyscall_channel_message_attachment_channel_t* syscall_channel_attachment = (void*)syscall_attachment_header;

					simple_memset(syscall_channel_attachment, 0, sizeof(*syscall_channel_attachment));

					if (pre_receive_peek) {
						syscall_channel_attachment->channel_id = FPROC_DID_MAX;
					} else {
						status = fproc_install_descriptor(fproc_current(), kernel_channel_attachment->channel, &fsyscall_channel_descriptor_class, &syscall_channel_attachment->channel_id);
						if (status != ferr_ok) {
							goto out;
						}
					}

					syscall_channel_attachment->header.type = fchannel_message_attachment_type_channel;
					syscall_channel_attachment->header.length = sizeof(*syscall_channel_attachment);
				} break;

				case fchannel_message_attachment_type_mapping: {
					fchannel_message_attachment_mapping_t* kernel_mapping_attachment = (void*)kernel_attachment_header;
					fsyscall_channel_message_attachment_mapping_t* syscall_mapping_attachment = (void*)syscall_attachment_header;

					simple_memset(syscall_mapping_attachment, 0, sizeof(*syscall_mapping_attachment));

					if (pre_receive_peek) {
						syscall_mapping_attachment->mapping_id = FPROC_DID_MAX;
					} else {
						status = fproc_install_descriptor(fproc_current(), kernel_mapping_attachment->mapping, &fsyscall_shared_page_class, &syscall_mapping_attachment->mapping_id);
						if (status != ferr_ok) {
							goto out;
						}
					}

					syscall_mapping_attachment->header.type = fchannel_message_attachment_type_mapping;
					syscall_mapping_attachment->header.length = sizeof(*syscall_mapping_attachment);
				} break;

				case fchannel_message_attachment_type_data: {
					fchannel_message_attachment_data_t* kernel_data_attachment = (void*)kernel_attachment_header;
					fsyscall_channel_message_attachment_data_t* syscall_data_attachment = (void*)syscall_attachment_header;

					if (kernel_data_attachment->flags & fchannel_message_attachment_data_flag_shared) {
						simple_memset(syscall_data_attachment, 0, sizeof(*syscall_data_attachment));

						if (pre_receive_peek) {
							syscall_data_attachment->target = FPROC_DID_MAX;
						} else {
							status = fproc_install_descriptor(fproc_current(), kernel_data_attachment->shared_data, &fsyscall_shared_page_class, &syscall_data_attachment->target);
							if (status != ferr_ok) {
								goto out;
							}
						}
					} else {
						if (pre_receive_peek) {
							simple_memset(syscall_data_attachment, 0, sizeof(*syscall_data_attachment));
						} else {
							syscall_data_attachment->header.next_offset = 0;

							if (syscall_data_attachment->length < kernel_data_attachment->length) {
								status = ferr_too_big;
								goto out;
							}

							status = ferro_uio_copy_out(kernel_data_attachment->copied_data, kernel_data_attachment->length, syscall_data_attachment->target);
							if (status != ferr_ok) {
								goto out;
							}
						}
					}

					syscall_data_attachment->length = kernel_data_attachment->length;
					syscall_data_attachment->flags = 0;
					if (kernel_data_attachment->flags & fchannel_message_attachment_data_flag_shared) {
						syscall_data_attachment->flags |= fsyscall_channel_message_attachment_data_flag_shared;
					}
					syscall_data_attachment->header.type = fchannel_message_attachment_type_data;
					syscall_data_attachment->header.length = sizeof(*syscall_data_attachment);
				} break;

				case fchannel_message_attachment_type_null: {
					fchannel_message_attachment_null_t* kernel_null_attachment = (void*)kernel_attachment_header;
					fsyscall_channel_message_attachment_null_t* syscall_null_attachment = (void*)syscall_attachment_header;

					simple_memset(syscall_null_attachment, 0, sizeof(*syscall_null_attachment));

					syscall_null_attachment->header.type = fchannel_message_attachment_type_null;
					syscall_null_attachment->header.length = sizeof(*syscall_null_attachment);
				} break;
			}

			++initialized_attachments;
		}
	}

	status = ferro_uio_copy_out(syscall_attachments_buffer, required_attachments_size, in_message.attachments_address);
	if (status != ferr_ok) {
		goto out;
	}

	// okay, should be smooth sailing from here on out

	if (!pre_receive_peek) {
		status = ferro_uio_copy_out(message.body, message.body_length, in_message.body_address);
		if (status != ferr_ok) {
			goto out;
		}
	}

	status = ferro_uio_copy_out(&message.conversation_id, sizeof(message.conversation_id), (uintptr_t)&user_in_out_message->conversation_id);
	if (status != ferr_ok) {
		goto out;
	}

	if (!pre_receive_peek) {
		// now let's consume the message
		fchannel_receive_locked(channel, false, &message, &lock_state);

		// now that we're sure the message is good, let's go ahead and clean up some of its resources we no longer need

		for (fchannel_message_attachment_header_t* kernel_attachment_header = (void*)message.attachments; kernel_attachment_header != NULL; kernel_attachment_header = (kernel_attachment_header->next_offset == 0) ? NULL : (void*)((char*)kernel_attachment_header + kernel_attachment_header->next_offset)) {
			switch (kernel_attachment_header->type) {
				case fchannel_message_attachment_type_channel: {
					fchannel_message_attachment_channel_t* kernel_channel_attachment = (void*)kernel_attachment_header;

					// the process retains the channel in the descriptor, so we don't need this reference anymore
					fchannel_release(kernel_channel_attachment->channel);
				} break;

				case fchannel_message_attachment_type_mapping: {
					fchannel_message_attachment_mapping_t* kernel_mapping_attachment = (void*)kernel_attachment_header;

					// the process retains the mapping in the descriptor, so we don't need this reference anymore
					fpage_mapping_release(kernel_mapping_attachment->mapping);
				} break;

				case fchannel_message_attachment_type_data: {
					fchannel_message_attachment_data_t* kernel_data_attachment = (void*)kernel_attachment_header;

					if (kernel_data_attachment->flags & fchannel_message_attachment_data_flag_shared) {
						// the process retains the mapping in the descriptor, so we don't need this reference anymore
						fpage_mapping_release(kernel_data_attachment->shared_data);
					} else {
						// the data was copied into the process-provided buffer, so we don't need this anymore
						FERRO_WUR_IGNORE(fmempool_free(kernel_data_attachment->copied_data));
					}
				} break;

				case fchannel_message_attachment_type_null:
				default:
					// nothing to clean up here
					break;
			}
		}

		FERRO_WUR_IGNORE(fmempool_free(message.body));
		FERRO_WUR_IGNORE(fmempool_free(message.attachments));
	}

out:
	// no matter whether we have enough space or not, we always want to tell the user exactly how much space we need.
	// if there's not enough space, then they need to know how much they should allocate.
	// if there's enough, then they need to know how much we actually used (which may be vital info e.g. for the body).
	status = ferro_uio_copy_out(&required_attachments_size, sizeof(required_attachments_size), (uintptr_t)&user_in_out_message->attachments_length);
	status = ferro_uio_copy_out(&message.body_length, sizeof(message.body_length), (uintptr_t)&user_in_out_message->body_length);

	// we only need to clean up attachments if we're doing a normal receive.
	// pre-receive peeks don't actually acquire any resources from the message attachments;
	// they only populate the information necessary for userspace to allocate some resources
	// of its own to handle the message with a normal receive later.
	if (status != ferr_ok && !pre_receive_peek) {
		fsyscall_channel_message_attachment_header_t* syscall_attachment_header = syscall_attachments_buffer;

		for (size_t i = 0; i < initialized_attachments && syscall_attachment_header != NULL; ++i) {
			switch (syscall_attachment_header->type) {
				case fchannel_message_attachment_type_channel: {
					fsyscall_channel_message_attachment_channel_t* syscall_channel_attachment = (void*)syscall_attachment_header;

					FERRO_WUR_IGNORE(fproc_uninstall_descriptor(fproc_current(), syscall_channel_attachment->channel_id));
				} break;

				case fchannel_message_attachment_type_mapping: {
					fsyscall_channel_message_attachment_mapping_t* syscall_mapping_attachment = (void*)syscall_attachment_header;

					FERRO_WUR_IGNORE(fproc_uninstall_descriptor(fproc_current(), syscall_mapping_attachment->mapping_id));
				} break;

				case fchannel_message_attachment_type_data: {
					fsyscall_channel_message_attachment_data_t* syscall_data_attachment = (void*)syscall_attachment_header;

					if (syscall_data_attachment->flags & fsyscall_channel_message_attachment_data_flag_shared) {
						FERRO_WUR_IGNORE(fproc_uninstall_descriptor(fproc_current(), syscall_data_attachment->target));
					} else {
						// the data was just copied into a user-provided buffer, so there's nothing to clean up here
					}
				} break;

				case fchannel_message_attachment_type_null:
				default:
					// nothing to clean up here
					break;
			}

			syscall_attachment_header = (syscall_attachment_header->next_offset == 0) ? NULL : (void*)((char*)syscall_attachment_header + syscall_attachment_header->next_offset);
		}
	}

	fchannel_unlock_receive(channel, &lock_state);

out_unlocked:
	if (channel) {
		desc_class->release(channel);
	}
	if (syscall_attachments_buffer) {
		FERRO_WUR_IGNORE(fmempool_free(syscall_attachments_buffer));
	}
	return status;
};

ferr_t fsyscall_handler_channel_close(uint64_t channel_id, uint8_t release_descriptor) {
	ferr_t status = ferr_ok;
	fchannel_t* channel = NULL;
	const fproc_descriptor_class_t* desc_class = NULL;

	status = fproc_lookup_descriptor(fproc_current(), channel_id, true, (void*)&channel, &desc_class);
	if (status != ferr_ok) {
		goto out;
	}

	if (desc_class != &fsyscall_channel_descriptor_class) {
		status = ferr_invalid_argument;
		goto out;
	}

	// we actually don't care what this returns.
	// no matter what it returns, it *does* ensure the channel is closed, so it doesn't matter to us what it returns.
	FERRO_WUR_IGNORE(fchannel_close(channel));

	if (release_descriptor) {
		FERRO_WUR_IGNORE(fproc_uninstall_descriptor(fproc_current(), channel_id));
	}

out:
	if (channel) {
		desc_class->release(channel);
	}
	return status;
};
