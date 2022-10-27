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

#include <libeve/server-channel.private.h>
#include <libeve/item.private.h>

static void eve_server_channel_destroy(eve_object_t* obj) {
	eve_server_channel_object_t* server_channel = (void*)obj;

	if (server_channel->destructor) {
		server_channel->destructor(server_channel->context);
	}

	if (server_channel->sys_server_channel) {
		sys_release(server_channel->sys_server_channel);
	}

	if (server_channel->monitor_item) {
		sys_release(server_channel->monitor_item);
	}

	sys_object_destroy(obj);
};

LIBEVE_STRUCT(eve_server_channel_handler_context) {
	eve_server_channel_object_t* server_channel;
	sys_channel_t* channel;
};

static void eve_server_channel_handler(void* _context) {
	eve_server_channel_handler_context_t* context = _context;

	context->server_channel->handler(context->server_channel->context, (void*)context->server_channel, context->channel);

	eve_release((void*)context->server_channel);
	LIBEVE_WUR_IGNORE(sys_mempool_free(context));
};

static void eve_server_channel_try_accept(eve_server_channel_object_t* server_channel) {
	ferr_t status = ferr_ok;

	while (true) {
		sys_channel_t* channel = NULL;
		eve_server_channel_handler_context_t* handler_context = NULL;

		status = sys_mempool_allocate(sizeof(*handler_context), NULL, (void*)&handler_context);
		if (status != ferr_ok) {
			break;
		}

		// this should never fail
		sys_abort_status(eve_retain((void*)server_channel));

		status = sys_server_channel_accept(server_channel->sys_server_channel, sys_server_channel_accept_flag_no_wait, &channel);
		if (status != ferr_ok) {
			eve_release((void*)server_channel);
			LIBEVE_WUR_IGNORE(sys_mempool_free(handler_context));
			break;
		}

		handler_context->server_channel = server_channel;
		handler_context->channel = channel;

		// see `eve_channel_try_receive` in channel.c for what we *should* do here in the future.
		// for now, we just drop the client on failure.
		status = eve_loop_enqueue(eve_loop_get_current(), eve_server_channel_handler, handler_context);
		if (status != ferr_ok) {
			sys_release(channel);
			eve_release((void*)server_channel);
			LIBEVE_WUR_IGNORE(sys_mempool_free(handler_context));
			continue;
		}
	}
};

static void eve_server_channel_handle_events(eve_item_t* self, sys_monitor_events_t events) {
	eve_server_channel_object_t* server_channel = (void*)self;

	if (events & sys_monitor_event_server_channel_client_arrived) {
		eve_server_channel_try_accept(server_channel);
	}
};

static sys_monitor_item_t* eve_server_channel_get_monitor_item(eve_item_t* self) {
	eve_server_channel_object_t* server_channel = (void*)self;
	return server_channel->monitor_item;
};

static void eve_server_channel_poll_after_attach(eve_item_t* self) {
	eve_server_channel_object_t* server_channel = (void*)self;
	eve_server_channel_try_accept(server_channel);
};

static void eve_server_channel_set_destructor(eve_item_t* self, eve_item_destructor_f destructor) {
	eve_server_channel_object_t* server_channel = (void*)self;
	server_channel->destructor = destructor;
};

static void* eve_server_channel_get_context(eve_item_t* self) {
	eve_server_channel_object_t* server_channel = (void*)self;
	return server_channel->context;
};

static const eve_item_interface_t eve_server_channel_item = {
	LIBEVE_ITEM_INTERFACE(NULL),
	.handle_events = eve_server_channel_handle_events,
	.get_monitor_item = eve_server_channel_get_monitor_item,
	.poll_after_attach = eve_server_channel_poll_after_attach,
	.set_destructor = eve_server_channel_set_destructor,
	.get_context = eve_server_channel_get_context,
};

static const eve_object_class_t eve_server_channel_class = {
	LIBSYS_OBJECT_CLASS_INTERFACE(&eve_server_channel_item.interface),
	.destroy = eve_server_channel_destroy,
};

ferr_t eve_server_channel_create(sys_server_channel_t* sys_server_channel, void* context, eve_server_channel_t** out_server_channel) {
	ferr_t status = ferr_ok;
	eve_server_channel_object_t* server_channel = NULL;

	if (sys_retain(sys_server_channel) != ferr_ok) {
		sys_server_channel = NULL;
		status = ferr_permanent_outage;
		goto out;
	}

	status = sys_object_new(&eve_server_channel_class, sizeof(*server_channel) - sizeof(server_channel->object), (void*)&server_channel);
	if (status != ferr_ok) {
		goto out;
	}

	simple_memset((char*)server_channel + sizeof(server_channel->object), 0, sizeof(*server_channel) - sizeof(server_channel->object));

	status = sys_monitor_item_create(sys_server_channel, sys_monitor_item_flag_enabled | sys_monitor_item_flag_active_high | sys_monitor_item_flag_edge_triggered, sys_monitor_event_item_deleted | sys_monitor_event_server_channel_client_arrived, server_channel, &server_channel->monitor_item);
	if (status != ferr_ok) {
		goto out;
	}

	server_channel->sys_server_channel = sys_server_channel;
	server_channel->context = context;

out:
	if (status == ferr_ok) {
		*out_server_channel = (void*)server_channel;
	} else if (server_channel) {
		eve_release((void*)server_channel);
	} else {
		if (sys_server_channel) {
			sys_release(sys_server_channel);
		}
	}
	return status;
};

void eve_server_channel_set_handler(eve_server_channel_t* obj, eve_server_channel_handler_f handler) {
	eve_server_channel_object_t* server_channel = (void*)obj;
	server_channel->handler = handler;
};

ferr_t eve_server_channel_target(eve_server_channel_t* obj, bool retain, sys_server_channel_t** out_sys_server_channel) {
	eve_server_channel_object_t* server_channel = (void*)obj;

	if (retain && sys_retain(server_channel->sys_server_channel) != ferr_ok) {
		return ferr_permanent_outage;
	}

	*out_sys_server_channel = server_channel->sys_server_channel;
	return ferr_ok;
};
