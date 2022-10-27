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

#include <libeve/channel.private.h>
#include <libeve/item.private.h>
#include <libeve/locks.h>

static bool eve_channel_destroy_outstanding_reply_iterator(void* context, simple_ghmap_t* hashmap, simple_ghmap_hash_t hash, const void* key, size_t key_size, void* entry, size_t entry_size) {
	eve_channel_outstanding_reply_t* info = entry;
	if (info->is_sync) {
		*info->sync.out_error = ferr_permanent_outage;
		sys_semaphore_up(info->sync.semaphore);
	}
	return true;
};

static void eve_channel_destroy(sys_object_t* obj) {
	eve_channel_object_t* channel = (void*)obj;

	if (channel->destructor) {
		channel->destructor(channel->context);
	}

	if (channel->sys_channel) {
		sys_release(channel->sys_channel);
	}

	if (channel->monitor_item) {
		// we shouldn't be inserted in any monitors currently
		// (since we can only be in monitors owned by loops, and loops are required to retain us before adding us)
		sys_release(channel->monitor_item);
	}

	if (channel->inited_outbox) {
		eve_channel_outbox_entry_t entries[16];
		size_t count;
		while ((count = simple_ring_dequeue(&channel->outbox, entries, sizeof(entries) / sizeof(*entries))) > 0) {
			for (size_t i = 0; i < count; ++i) {
				sys_release(entries[i].message);
				if (entries[i].is_sync) {
					sys_semaphore_up(entries[i].sync.semaphore);
				}
			}
		}
		simple_ring_destroy(&channel->outbox);
	}

	if (channel->inited_oustanding_replies) {
		// this shouldn't be necessary (anyone waiting for a reply should have a reference on the channel)
		simple_ghmap_for_each(&channel->outstanding_replies_table, eve_channel_destroy_outstanding_reply_iterator, NULL);
		simple_ghmap_destroy(&channel->outstanding_replies_table);
	}

	sys_object_destroy(obj);
};

LIBEVE_STRUCT(eve_channel_message_handler_context) {
	eve_channel_object_t* channel;
	sys_channel_message_t* message;
	eve_channel_reply_handler_f reply_handler;
	void* context;
	ferr_t status;
};

static void eve_channel_message_handler(void* _context) {
	eve_channel_message_handler_context_t* context = _context;

	if (context->reply_handler) {
		context->reply_handler(context->context, (void*)context->channel, context->message, context->status);
	} else {
		context->channel->message_handler(context->channel->context, (void*)context->channel, context->message);
	}

	eve_release((void*)context->channel);
	LIBEVE_WUR_IGNORE(sys_mempool_free(context));
};

static void eve_channel_try_receive(eve_channel_object_t* channel) {
	ferr_t status = ferr_ok;

	while (true) {
		sys_channel_message_t* message = NULL;
		eve_channel_message_handler_context_t* handler_context = NULL;
		eve_channel_outstanding_reply_t* outstanding_reply_ptr = NULL;
		sys_channel_conversation_id_t convo_id = sys_channel_conversation_id_none;
		eve_channel_outstanding_reply_t outstanding_reply;

		// always do fallible operations before receiving the message
		// because we can't put the message back in the channel if we fail later on

		status = sys_mempool_allocate(sizeof(*handler_context), NULL, (void*)&handler_context);
		if (status != ferr_ok) {
			break;
		}

		// this should never fail
		sys_abort_status(eve_retain((void*)channel));

		handler_context->channel = channel;

		status = sys_channel_receive(channel->sys_channel, sys_channel_receive_flag_no_wait, &message);
		if (status != ferr_ok) {
			eve_release((void*)channel);
			LIBEVE_WUR_IGNORE(sys_mempool_free(handler_context));
			break;
		}

		handler_context->message = message;
		handler_context->reply_handler = NULL;
		handler_context->context = NULL;
		handler_context->status = ferr_ok;

		convo_id = sys_channel_message_get_conversation_id(message);

		if (convo_id != sys_channel_conversation_id_none) {
			// check if this is a reply we're waiting for
			sys_mutex_lock(&channel->outstanding_replies_mutex);
			if (simple_ghmap_lookup_h(&channel->outstanding_replies_table, convo_id, false, 0, NULL, (void*)&outstanding_reply_ptr, NULL) == ferr_ok) {
				simple_memcpy(&outstanding_reply, outstanding_reply_ptr, sizeof(outstanding_reply));
				LIBEVE_WUR_IGNORE(simple_ghmap_clear_h(&channel->outstanding_replies_table, convo_id));
			}
			sys_mutex_unlock(&channel->outstanding_replies_mutex);
		}

		if (outstanding_reply_ptr) {
			// this is a reply we're waiting for
			if (outstanding_reply.is_sync) {
				// we actually don't need to enqueue any work
				// drop the resources we acquired earlier
				eve_release((void*)channel);
				LIBEVE_WUR_IGNORE(sys_mempool_free(handler_context));

				// now notify the waiter
				*outstanding_reply.sync.out_message = message;
				sys_semaphore_up(outstanding_reply.sync.semaphore);

				// continue
				continue;
			} else {
				handler_context->reply_handler = outstanding_reply.async.reply_handler;
				handler_context->context = outstanding_reply.async.context;
			}
		}

		// unfortunately, we can't do this before receiving the message.
		// if this fails, we'll have to drop the message :(
		// FIXME: handle this gracefully. maybe we could queue messages that we received
		//        but failed to schedule loop work for, or maybe we could reserve space in the
		//        work queue before receiving the message (this is probably the better option).
		status = eve_loop_enqueue(eve_loop_get_current(), eve_channel_message_handler, handler_context);
		if (status != ferr_ok) {
			sys_release(message);
			eve_release((void*)channel);
			LIBEVE_WUR_IGNORE(sys_mempool_free(handler_context));
			continue;
		}
	}
};

LIBEVE_STRUCT(eve_channel_message_send_error_handler_context) {
	eve_channel_object_t* channel;
	sys_channel_message_t* message;
	ferr_t error;
	eve_channel_reply_handler_f reply_handler;
	void* context;
};

static void eve_channel_message_send_error_handler(void* _context) {
	eve_channel_message_send_error_handler_context_t* context = _context;

	if (context->reply_handler) {
		context->reply_handler(context->context, (void*)context->channel, context->message, context->error);
	} else {
		context->channel->message_send_error_handler(context->channel->context, (void*)context->channel, context->message, context->error);
	}

	eve_release((void*)context->channel);
	LIBEVE_WUR_IGNORE(sys_mempool_free(context));
};

static void eve_channel_try_send_locked(eve_channel_object_t* channel) {
	ferr_t status = ferr_ok;

	if (!channel->can_send) {
		return;
	}

	while (true) {
		eve_channel_outbox_entry_t entry;
		size_t count = simple_ring_peek(&channel->outbox, &entry, 1);

		if (count != 1) {
			break;
		}

		status = sys_channel_send(channel->sys_channel, sys_channel_send_flag_no_wait, entry.message, NULL);
		if (status != ferr_ok) {
			if (status == ferr_no_wait) {
				channel->can_send = false;
				break;
			} else {
				eve_channel_message_send_error_handler_context_t* handler_context = NULL;
				ferr_t orig_status = status;
				eve_channel_outstanding_reply_t* outstanding_reply_ptr = NULL;
				sys_channel_conversation_id_t convo_id = sys_channel_conversation_id_none;
				eve_channel_outstanding_reply_t outstanding_reply;

				if (entry.wants_reply) {
					convo_id = sys_channel_message_get_conversation_id(entry.message);

					// find and clear the outstanding reply entry
					sys_mutex_lock(&channel->outstanding_replies_mutex);
					if (simple_ghmap_lookup_h(&channel->outstanding_replies_table, convo_id, false, 0, NULL, (void*)&outstanding_reply_ptr, NULL) == ferr_ok) {
						simple_memcpy(&outstanding_reply, outstanding_reply_ptr, sizeof(outstanding_reply));
						LIBEVE_WUR_IGNORE(simple_ghmap_clear_h(&channel->outstanding_replies_table, convo_id));
					}
					sys_mutex_unlock(&channel->outstanding_replies_mutex);
				}

				if (entry.is_sync && (!entry.wants_reply || outstanding_reply_ptr)) {
					// we only wake up synchronous waiters if:
					//   1. we weren't waiting for a reply (since we're the only ones that can wake them up)
					//   2. we were waiting for a reply and we found their outstanding reply entry.
					// if we were waiting for a reply but found no outstanding reply entry,
					// someone else has woken up the waiter, so we just use the normal error handler.

					// just write out the error
					// we wake the waiter later on
					*entry.sync.out_error = orig_status;

					if (entry.wants_reply) {
						// ...unless we were expecting a reply.
						// in that case, we only wake up the waiter if there's an error (which there is in this case).
						sys_semaphore_up(entry.sync.semaphore);
					}
				} else {
					status = sys_mempool_allocate(sizeof(*handler_context), NULL, (void*)&handler_context);
					if (status != ferr_ok) {
						// drop the message; we have no choice
						sys_release(entry.message);
					} else {
						// this should never fail
						sys_abort_status(eve_retain((void*)channel));

						handler_context->channel = channel;
						handler_context->message = entry.message;
						handler_context->error = orig_status;
						if (!entry.is_sync && entry.wants_reply && outstanding_reply_ptr) {
							handler_context->reply_handler = outstanding_reply.async.reply_handler;
							handler_context->context = outstanding_reply.async.context;
						} else {
							handler_context->reply_handler = NULL;
							handler_context->context = NULL;
						}

						status = eve_loop_enqueue(eve_loop_get_current(), eve_channel_message_send_error_handler, handler_context);
						if (status != ferr_ok) {
							// drop the message; we have no choice
							sys_release(entry.message);

							eve_release((void*)channel);
							LIBEVE_WUR_IGNORE(sys_mempool_free(handler_context));
						}
					}
				}
			}
		}

		// the message has been consumed
		// (either successfully sent or error handled)
		count = simple_ring_dequeue(&channel->outbox, &entry, 1);

		if (entry.is_sync && !entry.wants_reply) {
			sys_semaphore_up(entry.sync.semaphore);
		}
	}
};

static void eve_channel_peer_close_handler(void* context) {
	eve_channel_object_t* channel = context;

	channel->peer_close_handler(channel->context, (void*)channel);

	eve_release((void*)channel);
};

LIBEVE_STRUCT(eve_channel_peer_close_reply_handler_context) {
	eve_channel_object_t* channel;
	eve_channel_reply_handler_f reply_handler;
	void* context;
};

static void eve_channel_peer_close_reply_handler(void* _context) {
	eve_channel_peer_close_reply_handler_context_t* context = _context;

	context->reply_handler(context->context, (void*)context->channel, NULL, ferr_permanent_outage);

	eve_release((void*)context->channel);
	LIBEVE_WUR_IGNORE(sys_mempool_free(context));
};

// FIXME: we shouldn't clear outstanding replies before completely emptying our incoming message queue

static bool eve_channel_peer_closed_outstanding_replies_iterator(void* context, simple_ghmap_t* hashmap, simple_ghmap_hash_t hash, const void* key, size_t key_size, void* entry, size_t entry_size) {
	eve_channel_outstanding_reply_t* info = entry;
	eve_channel_object_t* channel = context;
	if (info->is_sync) {
		*info->sync.out_error = ferr_permanent_outage;
		sys_semaphore_up(info->sync.semaphore);
	} else {
		eve_channel_peer_close_reply_handler_context_t* handler_context = NULL;
		ferr_t status = sys_mempool_allocate(sizeof(*handler_context), NULL, (void*)&handler_context);

		if (status == ferr_ok) {
			// this should never fail
			sys_abort_status(eve_retain((void*)channel));

			handler_context->channel = context;
			handler_context->reply_handler = info->async.reply_handler;
			handler_context->context = info->async.context;

			status = eve_loop_enqueue(eve_loop_get_current(), eve_channel_peer_close_reply_handler, handler_context);
			if (status != ferr_ok) {
				eve_release((void*)channel);
				LIBEVE_WUR_IGNORE(sys_mempool_free(handler_context));
			}
		}
	}
	return true;
};

static void eve_channel_handle_events(eve_item_t* self, sys_monitor_events_t events) {
	eve_channel_object_t* channel = (void*)self;
	ferr_t status = ferr_ok;

	if (events & sys_monitor_event_channel_peer_closed) {
		// this should never fail
		sys_abort_status(eve_retain((void*)channel));

		// can't really do anything about failing here, so just ignore it
		LIBEVE_WUR_IGNORE(eve_loop_enqueue(eve_loop_get_current(), eve_channel_peer_close_handler, channel));

		// notify outstanding reply waiters
		sys_mutex_lock(&channel->outstanding_replies_mutex);
		simple_ghmap_for_each(&channel->outstanding_replies_table, eve_channel_peer_closed_outstanding_replies_iterator, channel);
		LIBEVE_WUR_IGNORE(simple_ghmap_clear_all(&channel->outstanding_replies_table));
		sys_mutex_unlock(&channel->outstanding_replies_mutex);
	}

	if (events & sys_monitor_event_channel_peer_queue_space_available) {
		sys_mutex_lock(&channel->outbox_mutex);
		channel->can_send = true;
		eve_channel_try_send_locked(channel);
		sys_mutex_unlock(&channel->outbox_mutex);
	}

	if (events & sys_monitor_event_channel_message_arrived) {
		eve_channel_try_receive(channel);
	}
};

static sys_monitor_item_t* eve_channel_get_monitor_item(eve_item_t* self) {
	eve_channel_object_t* channel = (void*)self;
	return channel->monitor_item;
};

static void eve_channel_poll_after_attach(eve_item_t* self) {
	eve_channel_object_t* channel = (void*)self;

	sys_mutex_lock(&channel->outbox_mutex);
	eve_channel_try_send_locked(channel);
	sys_mutex_unlock(&channel->outbox_mutex);

	eve_channel_try_receive(channel);
};

static void eve_channel_set_destructor(eve_item_t* self, eve_item_destructor_f destructor) {
	eve_channel_object_t* channel = (void*)self;
	channel->destructor = destructor;
};

static void* eve_channel_get_context(eve_item_t* self) {
	eve_channel_object_t* channel = (void*)self;
	return channel->context;
};

static const eve_item_interface_t eve_channel_item = {
	LIBEVE_ITEM_INTERFACE(NULL),
	.handle_events = eve_channel_handle_events,
	.get_monitor_item = eve_channel_get_monitor_item,
	.poll_after_attach = eve_channel_poll_after_attach,
	.set_destructor = eve_channel_set_destructor,
	.get_context = eve_channel_get_context,
};

static const eve_object_class_t eve_channel_class = {
	LIBSYS_OBJECT_CLASS_INTERFACE(&eve_channel_item.interface),
	.destroy = eve_channel_destroy,
};

const eve_object_class_t* eve_object_class_channel(void) {
	return &eve_channel_class;
};

ferr_t eve_channel_create(sys_channel_t* sys_channel, void* context, eve_channel_t** out_channel) {
	ferr_t status = ferr_ok;
	eve_channel_object_t* channel = NULL;

	if (sys_retain(sys_channel) != ferr_ok) {
		sys_channel = NULL;
		status = ferr_permanent_outage;
		goto out;
	}

	status = sys_object_new(&eve_channel_class, sizeof(*channel) - sizeof(channel->object), (void*)&channel);
	if (status != ferr_ok) {
		goto out;
	}

	simple_memset((char*)channel + sizeof(channel->object), 0, sizeof(*channel) - sizeof(channel->object));

	status = sys_monitor_item_create(sys_channel, sys_monitor_item_flag_enabled | sys_monitor_item_flag_active_high | sys_monitor_item_flag_edge_triggered, sys_monitor_event_item_deleted | sys_monitor_event_channel_peer_closed | sys_monitor_event_channel_message_arrived | sys_monitor_event_channel_peer_queue_space_available, channel, &channel->monitor_item);
	if (status != ferr_ok) {
		goto out;
	}

	channel->sys_channel = sys_channel;
	channel->context = context;
	channel->can_send = true;

	sys_mutex_init(&channel->outbox_mutex);

	status = simple_ring_init(&channel->outbox, sizeof(*channel->outbox_buffer), sizeof(channel->outbox_buffer) / sizeof(*channel->outbox_buffer), channel->outbox_buffer, simple_ghmap_allocate_sys_mempool, simple_ghmap_free_sys_mempool, NULL, simple_ring_flag_dynamic);
	if (status != ferr_ok) {
		goto out;
	}

	channel->inited_outbox = true;

	status = simple_ghmap_init(&channel->outstanding_replies_table, 16, 0, simple_ghmap_allocate_sys_mempool, simple_ghmap_free_sys_mempool, NULL, NULL, NULL, NULL, NULL, NULL);
	if (status != ferr_ok) {
		goto out;
	}

	channel->inited_oustanding_replies = true;

out:
	if (status == ferr_ok) {
		*out_channel = (void*)channel;
	} else if (channel) {
		eve_release((void*)channel);
	} else {
		if (sys_channel) {
			sys_release(sys_channel);
		}
	}
	return status;
};

void eve_channel_set_message_handler(eve_channel_t* obj, eve_channel_message_handler_f handler) {
	eve_channel_object_t* channel = (void*)obj;
	channel->message_handler = handler;
};

void eve_channel_set_peer_close_handler(eve_channel_t* obj, eve_channel_peer_close_handler_f handler) {
	eve_channel_object_t* channel = (void*)obj;
	channel->peer_close_handler = handler;
};

void eve_channel_set_message_send_error_handler(eve_channel_t* obj, eve_channel_message_send_error_handler_f handler) {
	eve_channel_object_t* channel = (void*)obj;
	channel->message_send_error_handler = handler;
};

ferr_t eve_channel_send(eve_channel_t* obj, sys_channel_message_t* message, bool synchronous) {
	ferr_t status = ferr_ok;
	eve_channel_object_t* channel = (void*)obj;
	eve_channel_outbox_entry_t entry;
	sys_semaphore_t semaphore;

	entry.message = message;
	entry.is_sync = synchronous;
	entry.wants_reply = false;
	if (synchronous) {
		entry.sync.semaphore = synchronous ? &semaphore : NULL;
		entry.sync.out_error = &status;
	}

	if (synchronous) {
		sys_semaphore_init(&semaphore, 0);
	}

	sys_mutex_lock(&channel->outbox_mutex);
	if (simple_ring_enqueue(&channel->outbox, &entry, 1) != 1) {
		status = ferr_temporary_outage;
		if (synchronous) {
			sys_semaphore_up(&semaphore);
		}
	} else {
		// try to send the message now, if possible
		eve_channel_try_send_locked(channel);
	}
	sys_mutex_unlock(&channel->outbox_mutex);

	if (synchronous) {
		// this can be a long wait, so use the suspendable version
		eve_semaphore_down(&semaphore);
	}

out:
	return status;
};

ferr_t eve_channel_target(eve_channel_t* obj, bool retain, sys_channel_t** out_sys_channel) {
	eve_channel_object_t* channel = (void*)obj;

	if (retain && sys_retain(channel->sys_channel) != ferr_ok) {
		return ferr_permanent_outage;
	}

	*out_sys_channel = channel->sys_channel;
	return ferr_ok;
};

static eve_channel_cancellation_token_t eve_channel_next_cancellation_token(eve_channel_object_t* channel) {
	eve_channel_cancellation_token_t cancellation_token;

retry:
	cancellation_token = __atomic_fetch_add(&channel->next_cancellation_token, 1, __ATOMIC_RELAXED);
	if (cancellation_token == eve_channel_cancellation_token_invalid) {
		goto retry;
	}

	return cancellation_token;
};

// TODO: disallow sending messages expecting replies if the peer has already closed their end

ferr_t eve_channel_send_with_reply_async(eve_channel_t* obj, sys_channel_message_t* message, eve_channel_reply_handler_f reply_handler, void* context) {
	ferr_t status = ferr_ok;
	eve_channel_object_t* channel = (void*)obj;
	eve_channel_outbox_entry_t entry;
	eve_channel_outstanding_reply_t* outstanding_reply;
	sys_channel_conversation_id_t convo_id = sys_channel_message_get_conversation_id(message);
	bool created = false;

	if (convo_id == sys_channel_conversation_id_none) {
		status = ferr_invalid_argument;
		goto out;
	}

	// the reply handler is only invoked by the message sending code if an error occurs
	entry.message = message;
	entry.is_sync = false;
	entry.wants_reply = true;
	entry.sync.semaphore = NULL;
	entry.sync.out_error = NULL;

	sys_mutex_lock(&channel->outstanding_replies_mutex);
	status = simple_ghmap_lookup_h(&channel->outstanding_replies_table, convo_id, true, sizeof(*outstanding_reply), &created, (void*)&outstanding_reply, NULL);
	if (status != ferr_ok) {
		sys_mutex_unlock(&channel->outstanding_replies_mutex);
		goto out;
	}
	if (!created) {
		sys_mutex_unlock(&channel->outstanding_replies_mutex);
		status = ferr_already_in_progress;
		goto out;
	}
	// the reply handler is only invoked by the message receiving code if a reply is successfully received
	// (which *should not* occur before we send the message)
	outstanding_reply->is_sync = false;
	outstanding_reply->async.reply_handler = reply_handler;
	outstanding_reply->async.context = context;
	outstanding_reply->cancellation_token = eve_channel_next_cancellation_token(channel);
	sys_mutex_unlock(&channel->outstanding_replies_mutex);

	sys_mutex_lock(&channel->outbox_mutex);
	if (simple_ring_enqueue(&channel->outbox, &entry, 1) != 1) {
		status = ferr_temporary_outage;
	} else {
		// try to send the message now, if possible
		eve_channel_try_send_locked(channel);
	}
	sys_mutex_unlock(&channel->outbox_mutex);

out:
	if (status != ferr_ok) {
		if (created) {
			// clean up the outstanding reply entry we created
			sys_mutex_lock(&channel->outstanding_replies_mutex);
			LIBEVE_WUR_IGNORE(simple_ghmap_clear_h(&channel->outstanding_replies_table, convo_id));
			sys_mutex_unlock(&channel->outstanding_replies_mutex);
		}
	}
	return status;
};

ferr_t eve_channel_send_with_reply_sync(eve_channel_t* obj, sys_channel_message_t* message, sys_channel_message_t** out_reply) {
	ferr_t status = ferr_ok;
	eve_channel_object_t* channel = (void*)obj;
	eve_channel_outbox_entry_t entry;
	eve_channel_outstanding_reply_t* outstanding_reply;
	sys_channel_conversation_id_t convo_id = sys_channel_message_get_conversation_id(message);
	bool created = false;
	sys_semaphore_t semaphore;

	if (convo_id == sys_channel_conversation_id_none) {
		status = ferr_invalid_argument;
		goto out;
	}

	sys_semaphore_init(&semaphore, 0);

	entry.message = message;
	entry.is_sync = true;
	entry.wants_reply = true;
	entry.sync.semaphore = &semaphore;
	entry.sync.out_error = &status;

	sys_mutex_lock(&channel->outstanding_replies_mutex);
	status = simple_ghmap_lookup_h(&channel->outstanding_replies_table, convo_id, true, sizeof(*outstanding_reply), &created, (void*)&outstanding_reply, NULL);
	if (status != ferr_ok) {
		sys_mutex_unlock(&channel->outstanding_replies_mutex);
		goto out;
	}
	if (!created) {
		sys_mutex_unlock(&channel->outstanding_replies_mutex);
		status = ferr_already_in_progress;
		goto out;
	}
	outstanding_reply->is_sync = true;
	outstanding_reply->sync.out_message = out_reply;
	outstanding_reply->sync.semaphore = &semaphore;
	outstanding_reply->sync.out_error = &status;
	outstanding_reply->cancellation_token = eve_channel_next_cancellation_token(channel);
	sys_mutex_unlock(&channel->outstanding_replies_mutex);

	sys_mutex_lock(&channel->outbox_mutex);
	if (simple_ring_enqueue(&channel->outbox, &entry, 1) != 1) {
		status = ferr_temporary_outage;
		sys_semaphore_up(&semaphore);
	} else {
		// try to send the message now, if possible
		eve_channel_try_send_locked(channel);
	}
	sys_mutex_unlock(&channel->outbox_mutex);

	// this can be a long wait, so use the suspendable version
	eve_semaphore_down(&semaphore);

out:
	if (status != ferr_ok) {
		if (created) {
			// clean up the outstanding reply entry we created
			sys_mutex_lock(&channel->outstanding_replies_mutex);
			LIBEVE_WUR_IGNORE(simple_ghmap_clear_h(&channel->outstanding_replies_table, convo_id));
			sys_mutex_unlock(&channel->outstanding_replies_mutex);
		}
	}
	return status;
};

ferr_t eve_channel_receive_conversation_async(eve_channel_t* obj, sys_channel_conversation_id_t convo_id, eve_channel_reply_handler_f reply_handler, void* context, eve_channel_cancellation_token_t* out_cancellation_token) {
	ferr_t status = ferr_ok;
	eve_channel_object_t* channel = (void*)obj;
	bool created = false;
	eve_channel_outstanding_reply_t* outstanding_reply = NULL;

	if (convo_id == sys_channel_conversation_id_none) {
		status = ferr_invalid_argument;
		goto out;
	}

	sys_mutex_lock(&channel->outstanding_replies_mutex);
	status = simple_ghmap_lookup_h(&channel->outstanding_replies_table, convo_id, true, sizeof(*outstanding_reply), &created, (void*)&outstanding_reply, NULL);
	if (status != ferr_ok) {
		sys_mutex_unlock(&channel->outstanding_replies_mutex);
		goto out;
	}
	if (!created) {
		sys_mutex_unlock(&channel->outstanding_replies_mutex);
		status = ferr_already_in_progress;
		goto out;
	}
	outstanding_reply->is_sync = false;
	outstanding_reply->async.reply_handler = reply_handler;
	outstanding_reply->async.context = context;
	outstanding_reply->cancellation_token = eve_channel_next_cancellation_token(channel);
	if (out_cancellation_token) {
		*out_cancellation_token = outstanding_reply->cancellation_token;
	}
	sys_mutex_unlock(&channel->outstanding_replies_mutex);

out:
	if (status != ferr_ok) {
		if (created) {
			// clean up the outstanding reply entry we created
			sys_mutex_lock(&channel->outstanding_replies_mutex);
			LIBEVE_WUR_IGNORE(simple_ghmap_clear_h(&channel->outstanding_replies_table, convo_id));
			sys_mutex_unlock(&channel->outstanding_replies_mutex);
		}
	}
	return status;
};

ferr_t eve_channel_receive_conversation_cancel(eve_channel_t* obj, sys_channel_conversation_id_t convo_id, eve_channel_cancellation_token_t cancellation_token) {
	ferr_t status = ferr_no_such_resource;
	eve_channel_object_t* channel = (void*)obj;
	eve_channel_outstanding_reply_t outstanding_reply;
	eve_channel_outstanding_reply_t* outstanding_reply_ptr = NULL;

	if (cancellation_token == eve_channel_cancellation_token_invalid) {
		status = ferr_invalid_argument;
		goto out;
	}

	sys_mutex_lock(&channel->outstanding_replies_mutex);
	if (simple_ghmap_lookup_h(&channel->outstanding_replies_table, convo_id, false, 0, NULL, (void*)&outstanding_reply_ptr, NULL) == ferr_ok) {
		if (outstanding_reply_ptr->cancellation_token == cancellation_token) {
			status = ferr_ok;
			simple_memcpy(&outstanding_reply, outstanding_reply_ptr, sizeof(outstanding_reply));
			LIBEVE_WUR_IGNORE(simple_ghmap_clear_h(&channel->outstanding_replies_table, convo_id));
		}
	}
	sys_mutex_unlock(&channel->outstanding_replies_mutex);

	if (status != ferr_ok) {
		goto out;
	}

	if (outstanding_reply.is_sync) {
		*outstanding_reply.sync.out_error = ferr_cancelled;
		sys_semaphore_up(outstanding_reply.sync.semaphore);
	} else {
		eve_channel_message_handler_context_t* handler_context = NULL;

		status = sys_mempool_allocate(sizeof(*handler_context), NULL, (void*)&handler_context);
		if (status != ferr_ok) {
			// ignore it
			status = ferr_ok;
			goto out;
		}

		// this should never fail
		sys_abort_status(eve_retain((void*)channel));

		handler_context->channel = channel;
		handler_context->context = outstanding_reply.async.context;
		handler_context->reply_handler = outstanding_reply.async.reply_handler;
		handler_context->message = NULL;
		handler_context->status = ferr_cancelled;

		status = eve_loop_enqueue(eve_loop_get_current(), eve_channel_message_handler, handler_context);
		if (status != ferr_ok) {
			// ignore it
			status = ferr_ok;
			eve_release((void*)channel);
			LIBEVE_WUR_IGNORE(sys_mempool_free(handler_context));
		}
	}

out:
	return status;
};

ferr_t eve_channel_receive_conversation_sync(eve_channel_t* obj, sys_channel_conversation_id_t convo_id, sys_channel_message_t** out_reply) {
	ferr_t status = ferr_ok;
	eve_channel_object_t* channel = (void*)obj;
	bool created = false;
	eve_channel_outstanding_reply_t* outstanding_reply = NULL;
	sys_semaphore_t semaphore;

	if (convo_id == sys_channel_conversation_id_none) {
		status = ferr_invalid_argument;
		goto out;
	}

	sys_semaphore_init(&semaphore, 0);

	sys_mutex_lock(&channel->outstanding_replies_mutex);
	status = simple_ghmap_lookup_h(&channel->outstanding_replies_table, convo_id, true, sizeof(*outstanding_reply), &created, (void*)&outstanding_reply, NULL);
	if (status != ferr_ok) {
		sys_mutex_unlock(&channel->outstanding_replies_mutex);
		goto out;
	}
	if (!created) {
		sys_mutex_unlock(&channel->outstanding_replies_mutex);
		status = ferr_already_in_progress;
		goto out;
	}
	outstanding_reply->is_sync = true;
	outstanding_reply->sync.out_message = out_reply;
	outstanding_reply->sync.semaphore = &semaphore;
	outstanding_reply->sync.out_error = &status;
	outstanding_reply->cancellation_token = eve_channel_next_cancellation_token(channel);
	sys_mutex_unlock(&channel->outstanding_replies_mutex);

	// this can be a long wait, so use the suspendable version
	eve_semaphore_down(&semaphore);

out:
	if (status != ferr_ok) {
		if (created) {
			// clean up the outstanding reply entry we created
			sys_mutex_lock(&channel->outstanding_replies_mutex);
			LIBEVE_WUR_IGNORE(simple_ghmap_clear_h(&channel->outstanding_replies_table, convo_id));
			sys_mutex_unlock(&channel->outstanding_replies_mutex);
		}
	}
	return status;
};

ferr_t eve_channel_conversation_create(eve_channel_t* obj, sys_channel_conversation_id_t* out_conversation_id) {
	eve_channel_object_t* channel = (void*)obj;
	return sys_channel_conversation_create(channel->sys_channel, out_conversation_id);
};
