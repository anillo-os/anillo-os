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

#include <libsys/libsys.private.h>
#include <libeve/libeve.h>
#include <stdatomic.h>
#include <libspooky/libspooky.h>
#include <vfsman/vfs.h>
#include <vfsman/ramdisk.h>
#include <vfs.server.h>

#define SYNC_LOG 1

#define VFSMAN_SERVER_NAME "org.anillo.vfsman"

LIBSYS_STRUCT(sysman_server) {
	const char* name;
	size_t name_length;
	eve_channel_t* channel;
};

LIBSYS_STRUCT(sysman_client) {
	eve_channel_t* channel;
};

static sys_mutex_t console_mutex = SYS_MUTEX_INIT;

static sys_mutex_t server_table_mutex = SYS_MUTEX_INIT;
static simple_ghmap_t server_table;

__attribute__((format(printf, 1, 2)))
static void sysman_log_f(const char* format, ...) {
	va_list args;

#if SYNC_LOG
	eve_mutex_lock(&console_mutex);
#endif

	va_start(args, format);
	sys_console_log_fv(format, args);
	va_end(args);

#if SYNC_LOG
	sys_mutex_unlock(&console_mutex);
#endif
};

static void sysman_server_close(void* context, eve_channel_t* channel) {
	sysman_server_t* server = context;
	ferr_t status = ferr_ok;

	eve_mutex_lock(&server_table_mutex);
	status = simple_ghmap_clear(&server_table, server->name, server->name_length);
	sys_mutex_unlock(&server_table_mutex);

	LIBSYS_WUR_IGNORE(eve_loop_remove_item(eve_loop_get_current(), channel));

	if (status != ferr_ok) {
		sysman_log_f("Failed to clear server entry from table on peer close: %d (%s; %s)\n", status, ferr_name(status), ferr_description(status));
	}
};

ferr_t sysman_register(const char* name, size_t name_length, sys_sysman_realm_t realm, sys_channel_t** out_channel) {
	ferr_t status = ferr_ok;
	sys_channel_t* our_side = NULL;
	sys_channel_t* their_side = NULL;
	eve_channel_t* eve_channel = NULL;
	bool created = false;
	sysman_server_t* server = NULL;
	bool remove_on_fail = false;

	if (realm != sys_sysman_realm_global) {
		// TODO
		status = ferr_unsupported;
		goto out_unlocked;
	}

	status = sys_channel_create_pair(&our_side, &their_side);
	if (status != ferr_ok) {
		goto out_unlocked;
	}

	eve_mutex_lock(&server_table_mutex);

	status = simple_ghmap_lookup(&server_table, name, name_length, true, SIZE_MAX, &created, (void*)&server, NULL);
	if (status != ferr_ok) {
		goto out;
	}

	if (!created) {
		status = ferr_resource_unavailable;
		goto out;
	}

	simple_memset(server, 0, sizeof(*server));

	remove_on_fail = true;

	// this *cannot* fail; nobody else has access to the table right now, so it *must* be a valid entry (that we just created)
	sys_abort_status_log(simple_ghmap_lookup_stored_key(&server_table, name, name_length, (void*)&server->name, &server->name_length));

	status = eve_channel_create(our_side, server, &eve_channel);
	if (status != ferr_ok) {
		goto out;
	}

	server->channel = eve_channel;

	eve_channel_set_peer_close_handler(eve_channel, sysman_server_close);

	status = eve_loop_add_item(eve_loop_get_main(), eve_channel);
	if (status != ferr_ok) {
		goto out;
	}

	*out_channel = their_side;
	their_side = NULL;

	// the loop holds on to the eve channel, and the eve channel holds on to the sys channel, so we can release those 2 objects

out:
	if (remove_on_fail && status != ferr_ok) {
		LIBSYS_WUR_IGNORE(simple_ghmap_clear(&server_table, name, name_length));
	}

	sys_mutex_unlock(&server_table_mutex);
out_unlocked:
	if (eve_channel) {
		eve_release(eve_channel);
	}
	if (our_side) {
		sys_release(our_side);
	}
	if (their_side) {
		sys_release(their_side);
	}
	return status;
};

ferr_t sysman_connect(const char* name, size_t name_length, sys_channel_t** out_channel) {
	ferr_t status = ferr_ok;
	sys_channel_t* our_side = NULL;
	sys_channel_t* their_side = NULL;
	sysman_server_t* server = NULL;
	sys_channel_message_t* message = NULL;

	status = sys_channel_message_create(0, &message);
	if (status != ferr_ok) {
		goto out_unlocked;
	}

	status = sys_channel_create_pair(&our_side, &their_side);
	if (status != ferr_ok) {
		goto out_unlocked;
	}

	status = sys_channel_message_attach_channel(message, our_side, NULL);
	if (status != ferr_ok) {
		goto out_unlocked;
	}

	// attaching the channel consumes it
	our_side = NULL;

	eve_mutex_lock(&server_table_mutex);

	status = simple_ghmap_lookup(&server_table, name, name_length, false, SIZE_MAX, NULL, (void*)&server, NULL);
	if (status != ferr_ok) {
		goto out;
	}

	status = eve_channel_send(server->channel, message, false);
	if (status != ferr_ok) {
		goto out;
	}

	// successfully sending the message consumes it
	message = NULL;

	*out_channel = their_side;
	their_side = NULL;

	sysman_log_f("connected client to %.*s\n", (int)name_length, name);

out:
	sys_mutex_unlock(&server_table_mutex);
out_unlocked:
	if (message) {
		sys_release(message);
	}
	if (our_side) {
		sys_release(our_side);
	}
	if (their_side) {
		sys_release(their_side);
	}
	return status;
};

static void client_channel_destructor(void* context) {
	sysman_client_t* client = context;

	LIBSYS_WUR_IGNORE(sys_mempool_free(client));
};

static void client_channel_close_handler(void* context, eve_channel_t* channel) {
	LIBSYS_WUR_IGNORE(eve_loop_remove_item(eve_loop_get_current(), channel));
};

static void client_channel_message_handler(void* context, eve_channel_t* channel, sys_channel_message_t* message) {
	sysman_client_t* client = context;
	sys_channel_conversation_id_t convo_id = sys_channel_message_get_conversation_id(message);
	sys_channel_message_t* reply = NULL;
	sys_sysman_rpc_call_t* rpc_call = NULL;

	if (convo_id == sys_channel_conversation_id_none || sys_channel_message_length(message) < sizeof(rpc_call->header)) {
		goto out;
	}

	rpc_call = sys_channel_message_data(message);

	switch (rpc_call->header.function) {
		case sys_sysman_rpc_function_connect: {
			size_t name_length = sys_channel_message_length(message) - offsetof(sys_sysman_rpc_call_connect_t, name);
			ferr_t status = ferr_ok;
			sys_channel_t* connected_channel = NULL;
			sys_sysman_rpc_reply_connect_t* reply_data = NULL;

			status = sys_channel_message_create(sizeof(*reply_data), &reply);
			if (status != ferr_ok) {
				goto out;
			}

			sys_channel_message_set_conversation_id(reply, convo_id);

			reply_data = sys_channel_message_data(reply);
			reply_data->header.function = sys_sysman_rpc_function_connect;

			status = sysman_connect(rpc_call->connect.name, name_length, &connected_channel);
			if (status == ferr_ok) {
				status = sys_channel_message_attach_channel(reply, connected_channel, NULL);
			}

			if (status != ferr_ok) {
				if (connected_channel) {
					sys_release(connected_channel);
				}
			}

			reply_data->header.status = status;
		} break;

		case sys_sysman_rpc_function_register: {
			size_t name_length = sys_channel_message_length(message) - offsetof(sys_sysman_rpc_call_register_t, name);
			ferr_t status = ferr_ok;
			sys_channel_t* server_channel = NULL;
			sys_sysman_rpc_reply_register_t* reply_data = NULL;

			status = sys_channel_message_create(sizeof(*reply_data), &reply);
			if (status != ferr_ok) {
				goto out;
			}

			sys_channel_message_set_conversation_id(reply, convo_id);

			reply_data = sys_channel_message_data(reply);
			reply_data->header.function = sys_sysman_rpc_function_register;

			status = sysman_register(rpc_call->register_.name, name_length, rpc_call->register_.realm, &server_channel);
			if (status == ferr_ok) {
				status = sys_channel_message_attach_channel(reply, server_channel, NULL);
			}

			if (status != ferr_ok) {
				if (server_channel) {
					sys_release(server_channel);
				}
			}

			reply_data->header.status = status;
		} break;

		case sys_sysman_rpc_function_subchannel: {
			ferr_t status = ferr_ok;
			sys_channel_t* subchannel = NULL;
			sys_sysman_rpc_reply_subchannel_t* reply_data = NULL;

			status = sys_channel_message_create(sizeof(*reply_data), &reply);
			if (status != ferr_ok) {
				goto out;
			}

			sys_channel_message_set_conversation_id(reply, convo_id);

			reply_data = sys_channel_message_data(reply);
			reply_data->header.function = sys_sysman_rpc_function_register;

			status = sys_sysman_create_subchannel(&subchannel);
			if (status == ferr_ok) {
				status = sys_channel_message_attach_channel(reply, subchannel, NULL);
			}

			if (status != ferr_ok) {
				if (subchannel) {
					sys_release(subchannel);
				}
			}

			reply_data->header.status = status;
		} break;

		default:
			goto out;
	}

	if (eve_channel_send(channel, reply, false) == ferr_ok) {
		// successfully sending the reply consumes it
		reply = NULL;
	}

out:
	if (message) {
		sys_release(message);
	}
	if (reply) {
		sys_release(reply);
	}
};

ferr_t sys_sysman_create_subchannel(sys_channel_t** out_subchannel) {
	ferr_t status = ferr_ok;
	sys_channel_t* our_side = NULL;
	sys_channel_t* their_side = NULL;
	eve_channel_t* eve_channel = NULL;
	sysman_client_t* client = NULL;

	status = sys_mempool_allocate(sizeof(*client), NULL, (void*)&client);
	if (status != ferr_ok) {
		goto out;
	}

	status = sys_channel_create_pair(&our_side, &their_side);
	if (status != ferr_ok) {
		goto out;
	}

	status = eve_channel_create(our_side, NULL, &eve_channel);
	if (status != ferr_ok) {
		goto out;
	}

	client->channel = eve_channel;

	eve_item_set_destructor(eve_channel, client_channel_destructor);
	eve_channel_set_peer_close_handler(eve_channel, client_channel_close_handler);
	eve_channel_set_message_handler(eve_channel, client_channel_message_handler);

	// the destructor will take care of freeing this
	client = NULL;

	status = eve_loop_add_item(eve_loop_get_main(), eve_channel);
	if (status != ferr_ok) {
		goto out;
	}

	*out_subchannel = their_side;
	their_side = NULL;

	// the loop holds on to the eve channel, and the eve channel holds on to the sys channel, so we can release those 2 objects

out:
	if (eve_channel) {
		eve_release(eve_channel);
	}
	if (our_side) {
		sys_release(our_side);
	}
	if (their_side) {
		sys_release(their_side);
	}
	if (client) {
		LIBSYS_WUR_IGNORE(sys_mempool_free(client));
	}
	return status;
};

static sys_shared_memory_object_t ramdisk_memory = {
	.object = {
		.flags = 0,
		.object_class = NULL, // filled in at runtime
		.reference_count = UINT32_MAX,
	},

	// the ramdisk mapping DID is always the first descriptor
	.did = 0,
};

static sys_channel_object_t pciman_channel = {
	.object = {
		.flags = sys_object_flag_immortal,
		.object_class = &__sys_object_class_channel,
		.reference_count = 1,
	},

	// the pciman server channel DID is always the second descriptor
	.channel_did = 1,
};

static size_t ramdisk_memory_page_count = 0;

static void start_process(const char* filename) {
	sys_proc_t* proc = NULL;
	sys_file_t* file = NULL;

	sys_abort_status_log(sys_file_open(filename, &file));

	sysman_log_f("starting %s...\n", filename);
	sys_abort_status_log(sys_proc_create(file, NULL, 0, sys_proc_flag_resume | sys_proc_flag_detach, &proc));
	sysman_log_f("%s started with PID = %llu\n", filename, sys_proc_id(proc));

	sys_release(file);
	file = NULL;

	sys_release(proc);
	proc = NULL;
};

static void start_managers(void* context) {
	start_process("/sys/netman/netman");
	start_process("/sys/usbman/usbman");
};

void start(void) asm("start");
void start(void) {
	eve_loop_t* main_loop = NULL;
	sys_channel_t* vfsman_channel = NULL;
	sysman_server_t* pciman_server = NULL;
	bool created = false;
	eve_channel_t* pciman_eve_channel = NULL;

	sys_abort_status_log(sys_init_core_full());
	sys_abort_status_log(sys_init_support());

	ferr_t status = ferr_ok;
	sys_channel_t* subchannel = NULL;

	sys_abort_status_log(sys_sysman_create_subchannel(&subchannel));
	sys_abort_status_log(eve_channel_create(subchannel, NULL, &__sys_sysman_eve_channel));
	sys_abort_status_log(eve_loop_add_item(eve_loop_get_main(), __sys_sysman_eve_channel));

	sys_abort_status_log(simple_ghmap_init_string_to_generic(&server_table, 64, sizeof(sysman_server_t), simple_ghmap_allocate_sys_mempool, simple_ghmap_free_sys_mempool, NULL));

	// register the pciman server
	sys_abort_status_log(simple_ghmap_lookup(&server_table, "org.anillo.pciman", SIZE_MAX, true, SIZE_MAX, &created, (void*)&pciman_server, NULL));
	if (!created) {
		sys_console_log("failed to register pciman server: entry was not freshly created (this should be impossible)");
		sys_abort();
	}

	sys_abort_status_log(simple_ghmap_lookup_stored_key(&server_table, "org.anillo.pciman", SIZE_MAX, (void*)&pciman_server->name, &pciman_server->name_length));
	sys_abort_status_log(eve_channel_create((void*)&pciman_channel, pciman_server, &pciman_server->channel));

	ramdisk_memory.object.object_class = sys_object_class_shared_memory();

	sys_abort_status_log(sys_shared_memory_page_count((void*)&ramdisk_memory, &ramdisk_memory_page_count));

	main_loop = eve_loop_get_main();

	vfsman_init();
	vfsman_ramdisk_init((void*)&ramdisk_memory);

	sys_abort_status_log(sysman_register(VFSMAN_SERVER_NAME, sizeof(VFSMAN_SERVER_NAME) - 1, sys_sysman_realm_global, &vfsman_channel));
	sys_abort_status_log(vfsman_serve_explicit(main_loop, vfsman_channel));
	vfsman_channel = NULL;

	sys_abort_status_log(eve_loop_enqueue(main_loop, start_managers, NULL));

	eve_loop_run(main_loop);

	// should never get here
	sys_abort();
};
