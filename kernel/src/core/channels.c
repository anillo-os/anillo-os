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

#include <ferro/core/channels.private.h>
#include <ferro/core/mempool.h>
#include <ferro/core/panic.h>
#include <ferro/core/ghmap.h>
#include <ferro/core/paging.h>

//
// IMPORTANT
//
// when waking up waitqs and incrementing semaphores, always increment the semaphore before waking up
// the waitq and always wake up the waitq with the channel mutex held (with a few exceptions for special cases).
//
// the reason to increment the semaphore before waking up the waitq is because the operation must already be
// fully completed (with the lock held) before waiters can be awoken, since some waiters may trigger operations
// that depend on the semaphore state but do not require on the lock. an example of this would be a message send
// immediately followed by a message receive. if someone is waiting on the waitq for a message send operation
// (on their peer's end), they may immediately try to receive the message on their end and run into `ferr_no_wait`
// because the semaphore hadn't been incremented yet.
//
// the reason to hold the channel mutex for waking up waitqs is that, if you wake them up without the mutex held,
// they can be awoken in a different order than the order of events that occurred. this is a problem for certain
// waiters (e.g. the userspace monitor API) which rely on the order of wake-ups to determine certain properties
// and events on the channel.
//
// the only special case where you don't need to be holding the mutex to wake up the waitq is for close events.
// this is because the close event happens only once and there's no guaranteed order of events between the close
// event and any other events (it can occur at any time).
//

// TODO: holding the mutex while incrementing semaphores is a pessimization, since anyone waiting for semaphores
//       will immediately need to acquire the lock. we should find a way to avoid this requirement while still satisfying
//       event ordering constraints. this basically means we need to find a way that we can safely wake up the waitq outside the lock

void fchannel_init(void) {
	// nothing for now
};

static void fchannel_destroy(fchannel_private_t* private_channel) {
	// TODO: determine the optimal size for this temporary array
	fchannel_message_t tmp[4];
	size_t dequeued = 0;

	// private_channel is channel 0 in the pair.
	// both the channel and its peer have been fully released, so we need to destroy both here.

	// both ends have also been closed.

	// destroy all messages left in the queues
	while ((dequeued = simple_ring_dequeue(&private_channel->messages, &tmp[0], sizeof(tmp) / sizeof(*tmp))) > 0) {
		for (size_t i = 0; i < dequeued; ++i) {
			fchannel_message_destroy(&tmp[i]);
		}
	}

	while ((dequeued = simple_ring_dequeue(&private_channel->peer->messages, &tmp[0], sizeof(tmp) / sizeof(*tmp))) > 0) {
		for (size_t i = 0; i < dequeued; ++i) {
			fchannel_message_destroy(&tmp[i]);
		}
	}

	// now destroy both message queues
	simple_ring_destroy(&private_channel->messages);
	simple_ring_destroy(&private_channel->peer->messages);

	// now free both of them
	FERRO_WUR_IGNORE(fmempool_free(private_channel->peer));
	FERRO_WUR_IGNORE(fmempool_free(private_channel));
};

ferr_t fchannel_retain(fchannel_t* channel) {
	fchannel_private_t* private_channel = (void*)channel;
	return frefcount_increment(&private_channel->closure_refcount);
};

void fchannel_release(fchannel_t* channel) {
	fchannel_private_t* private_channel = (void*)channel;

	if (frefcount_decrement(&private_channel->closure_refcount) == ferr_permanent_outage) {
		// okay, this end of the channel has been released; make sure it's closed.
		FERRO_WUR_IGNORE(fchannel_close((void*)private_channel));

		// now let's release its reference on the pair
		if (frefcount_decrement(&fchannel_get_0(private_channel)->channel_0.destruction_refcount) == ferr_permanent_outage) {
			// both ends of the channel have been closed and released; let's destroy the channel now.
			fchannel_destroy(fchannel_get_0(private_channel));
		}
	}
};

ferr_t fchannel_new_pair(fchannel_t** out_channel_1, fchannel_t** out_channel_2) {
	ferr_t status = ferr_ok;
	fchannel_private_t* channels[2] = {0};
	bool destroy_ring_on_fail[2] = {0};

	status = fmempool_allocate(sizeof(*channels[0]), NULL, (void*)&channels[0]);
	if (status != ferr_ok) {
		goto out;
	}

	simple_memset(channels[0], 0, sizeof(*channels[0]));

	status = fmempool_allocate(sizeof(*channels[1]), NULL, (void*)&channels[1]);
	if (status != ferr_ok) {
		goto out;
	}

	simple_memset(channels[1], 0, sizeof(*channels[1]));

	status = simple_ring_init(&channels[0]->messages, sizeof(fchannel_message_t), 64, NULL, simple_ghmap_allocate_mempool, simple_ghmap_free_mempool, NULL, 0);
	if (status != ferr_ok) {
		goto out;
	}

	destroy_ring_on_fail[0] = true;

	status = simple_ring_init(&channels[1]->messages, sizeof(fchannel_message_t), 64, NULL, simple_ghmap_allocate_mempool, simple_ghmap_free_mempool, NULL, 0);
	if (status != ferr_ok) {
		goto out;
	}

	destroy_ring_on_fail[1] = true;

	channels[0]->is_channel_0 = true;
	channels[1]->is_channel_0 = false;

	// each channel maintains its own separate closure refcount
	frefcount_init(&channels[0]->closure_refcount);
	frefcount_init(&channels[1]->closure_refcount);

	fwaitq_init(&channels[0]->base.message_arrival_waitq);
	fwaitq_init(&channels[0]->base.queue_empty_waitq);
	fwaitq_init(&channels[0]->base.queue_removal_waitq);
	fwaitq_init(&channels[0]->base.close_waitq);
	fwaitq_init(&channels[0]->base.queue_full_waitq);

	fwaitq_init(&channels[1]->base.message_arrival_waitq);
	fwaitq_init(&channels[1]->base.queue_empty_waitq);
	fwaitq_init(&channels[1]->base.queue_removal_waitq);
	fwaitq_init(&channels[1]->base.close_waitq);
	fwaitq_init(&channels[1]->base.queue_full_waitq);

	channels[0]->peer = channels[1];
	channels[1]->peer = channels[0];

	flock_mutex_init(&channels[0]->mutex);
	flock_mutex_init(&channels[1]->mutex);

	flock_semaphore_init(&channels[0]->message_removal_semaphore, 0);
	flock_semaphore_init(&channels[1]->message_removal_semaphore, 0);

	flock_semaphore_init(&channels[0]->message_insertion_semaphore, 64);
	flock_semaphore_init(&channels[1]->message_insertion_semaphore, 64);

	channels[1]->channel_1.next_conversation_id = 1;
	channels[1]->channel_1.next_message_id = 0;

	// the destruction refcount is shared between the pair, thus we must initialize it to 2
	// the destruction refcount should only ever have a value of 0, 1, or 2
	frefcount_init(&channels[0]->channel_0.destruction_refcount);
	FERRO_WUR_IGNORE(frefcount_increment(&channels[0]->channel_0.destruction_refcount));

out:
	if (status == ferr_ok) {
		*out_channel_1 = (void*)channels[0];
		*out_channel_2 = (void*)channels[1];
	} else {
		if (channels[0]) {
			if (destroy_ring_on_fail[0]) {
				simple_ring_destroy(&channels[0]->messages);
			}
			FERRO_WUR_IGNORE(fmempool_free(channels[0]));
		}
		if (channels[1]) {
			if (destroy_ring_on_fail[1]) {
				simple_ring_destroy(&channels[1]->messages);
			}
			FERRO_WUR_IGNORE(fmempool_free(channels[1]));
		}
	}

	return status;
};

fchannel_conversation_id_t fchannel_next_conversation_id(fchannel_t* channel) {
	fchannel_private_t* private_channel = (void*)channel;
	fchannel_conversation_id_t conversation_id = fchannel_conversation_id_none;

retry:
	conversation_id = __atomic_fetch_add(&fchannel_get_1(private_channel)->channel_1.next_conversation_id, 1, __ATOMIC_RELAXED);

	// try again if we got a reserved conversation ID
	if (conversation_id == fchannel_conversation_id_none) {
		goto retry;
	}

	return conversation_id;
};

ferr_t fchannel_lock_send(fchannel_t* channel, fchannel_send_flags_t flags, fchannel_send_lock_state_t* out_lock_state) {
	fchannel_private_t* private_channel = (void*)channel;
	ferr_t status = ferr_ok;
	bool increment_semaphore_on_fail = false;
	bool unlock_on_fail = false;

	if ((flags & fchannel_send_flag_no_wait) != 0) {
		if (flock_semaphore_try_down(&private_channel->peer->message_insertion_semaphore) != ferr_ok) {
			status = ferr_no_wait;
			goto out;
		}
	} else {
		if (flags & fchannel_send_kernel_flag_interruptible) {
			status = flock_semaphore_down_interruptible(&private_channel->peer->message_insertion_semaphore);
			if (status != ferr_ok) {
				goto out;
			}
		} else {
			flock_semaphore_down(&private_channel->peer->message_insertion_semaphore);
		}
	}

	increment_semaphore_on_fail = true;

	// let's check quickly to see if the channel is closed for receiving
	// when our peer is closed for receiving, that implies that we're closed for sending.
	if ((private_channel->peer->flags & fchannel_flag_closed_receive) != 0) {
		status = ferr_permanent_outage;
		goto out;
	}

	// now let's acquire the lock to try to insert our message
	flock_mutex_lock(&private_channel->peer->mutex);

	unlock_on_fail = true;

	// check the flags again because the channel may have been closed while
	// we were waiting for the lock
	if ((private_channel->peer->flags & fchannel_flag_closed_receive) != 0) {
		status = ferr_permanent_outage;
		goto out;
	}

out:
	if (status != ferr_ok) {
		if (unlock_on_fail) {
			flock_mutex_unlock(&private_channel->peer->mutex);
		}

		if (increment_semaphore_on_fail) {
			flock_semaphore_up(&private_channel->peer->message_insertion_semaphore);
		}
	}

	out_lock_state->enqueued = false;
	out_lock_state->flags = flags;
	out_lock_state->queue_filled = false;

	return status;
};

void fchannel_unlock_send(fchannel_t* channel, fchannel_send_lock_state_t* in_lock_state) {
	fchannel_private_t* private_channel = (void*)channel;

	if (!in_lock_state->enqueued) {
		// if we didn't actually enqueue anything, increment the insertion semaphore back up.
		flock_semaphore_up(&private_channel->peer->message_insertion_semaphore);
	} else {
		// otherwise, we did enqueue a message, so there's a bit more logic to perform

		// there's now a message available; increment the removal semaphore 
		flock_semaphore_up(&private_channel->peer->message_removal_semaphore);

		// now wake up the message arrival waitq
		fwaitq_wake_many(&private_channel->peer->base.message_arrival_waitq, SIZE_MAX);

		// if we filled up the queue, wake up anyone who wants to know
		if (in_lock_state->queue_filled) {
			fwaitq_wake_many(&private_channel->peer->base.queue_full_waitq, SIZE_MAX);
		}
	}

	flock_mutex_unlock(&private_channel->peer->mutex);
};

void fchannel_send_locked(fchannel_t* channel, fchannel_message_t* in_out_message, fchannel_send_lock_state_t* in_out_lock_state) {
	fchannel_private_t* private_channel = (void*)channel;

	// assign a conversation ID now (if we wanted to do that)
	if ((in_out_lock_state->flags & fchannel_send_flag_start_conversation) != 0) {
		in_out_message->conversation_id = fchannel_next_conversation_id(channel);
	}

	// assign a message ID now
	in_out_message->message_id = fchannel_next_message_id(channel);

	if (simple_ring_enqueue(&private_channel->peer->messages, in_out_message, 1) != 1) {
		// like connecting to a server channel, if we've successfully decremented the insertion
		// semaphore, acquired the mutex, and seen that the channel is still open,
		// it is IMPOSSIBLE for enqueuing the message to fail.
		fpanic("Invalid peer message queue state");
	}

	// check if we filled up the queue
	// if we did, then we need to wake up the queue_full waitq later
	if (simple_ring_queued_count(&private_channel->peer->messages) == 64) {
		in_out_lock_state->queue_filled = true;
	}

	in_out_lock_state->enqueued = true;
};

ferr_t fchannel_send(fchannel_t* channel, fchannel_send_flags_t flags, fchannel_message_t* in_out_message) {
	fchannel_private_t* private_channel = (void*)channel;
	ferr_t status = ferr_ok;
	bool increment_semaphore_on_fail = false;
	bool queue_filled = false;

	if ((flags & fchannel_send_flag_no_wait) != 0) {
		if (flock_semaphore_try_down(&private_channel->peer->message_insertion_semaphore) != ferr_ok) {
			status = ferr_no_wait;
			goto out;
		}
	} else {
		if (flags & fchannel_send_kernel_flag_interruptible) {
			status = flock_semaphore_down_interruptible(&private_channel->peer->message_insertion_semaphore);
			if (status != ferr_ok) {
				goto out;
			}
		} else {
			flock_semaphore_down(&private_channel->peer->message_insertion_semaphore);
		}
	}

	increment_semaphore_on_fail = true;

	// let's check quickly to see if the channel is closed for receiving
	// when our peer is closed for receiving, that implies that we're closed for sending.
	if ((private_channel->peer->flags & fchannel_flag_closed_receive) != 0) {
		status = ferr_permanent_outage;
		goto out;
	}

	// now let's acquire the lock to try to insert our message
	flock_mutex_lock(&private_channel->peer->mutex);

	// check the flags again because the channel may have been closed while
	// we were waiting for the lock
	if ((private_channel->peer->flags & fchannel_flag_closed_receive) != 0) {
		status = ferr_permanent_outage;
		flock_mutex_unlock(&private_channel->peer->mutex);
		goto out;
	}

	// assign a conversation ID now (if we wanted to do that)
	if ((flags & fchannel_send_flag_start_conversation) != 0) {
		in_out_message->conversation_id = fchannel_next_conversation_id(channel);
	}

	// assign a message ID now
	in_out_message->message_id = fchannel_next_message_id(channel);

	// now let's insert our message
	if (simple_ring_enqueue(&private_channel->peer->messages, in_out_message, 1) != 1) {
		// like connecting to a server channel, if we've successfully decremented the insertion
		// semaphore, acquired the mutex, and seen that the channel is still open,
		// it is IMPOSSIBLE for enqueuing the message to fail.
		fpanic("Invalid peer message queue state");
	}

	// check if we filled up the queue
	// if we did, then we need to wake up the queue_full waitq later
	if (simple_ring_queued_count(&private_channel->peer->messages) == 64) {
		queue_filled = true;
	}

	// there's now a message available; increment the removal semaphore
	flock_semaphore_up(&private_channel->peer->message_removal_semaphore);

	// now wake up the message arrival waitq
	fwaitq_wake_many(&private_channel->peer->base.message_arrival_waitq, SIZE_MAX);

	// if we filled up the queue, wake up anyone who wants to know
	if (queue_filled) {
		fwaitq_wake_many(&private_channel->peer->base.queue_full_waitq, SIZE_MAX);
	}

	flock_mutex_unlock(&private_channel->peer->mutex);

out:
	if (status != ferr_ok) {
		if (increment_semaphore_on_fail) {
			flock_semaphore_up(&private_channel->peer->message_insertion_semaphore);
		}
	}

	return status;
};

ferr_t fchannel_lock_receive(fchannel_t* channel, fchannel_receive_flags_t flags, fchannel_receive_lock_state_t* out_lock_state) {
	fchannel_private_t* private_channel = (void*)channel;
	ferr_t status = ferr_ok;
	bool increment_on_fail = false;
	bool unlock_on_fail = false;

	if ((flags & fchannel_receive_flag_no_wait) != 0) {
		if (flock_semaphore_try_down(&private_channel->message_removal_semaphore) != ferr_ok) {
			status = ferr_no_wait;
			goto out;
		}
	} else {
		if (flags & fchannel_receive_flag_interruptible) {
			status = flock_semaphore_down_interruptible(&private_channel->message_removal_semaphore);
			if (status != ferr_ok) {
				goto out;
			}
		} else {
			flock_semaphore_down(&private_channel->message_removal_semaphore);
		}
	}

	increment_on_fail = true;

	// now let's acquire the lock (so we can remove our message later)
	flock_mutex_lock(&private_channel->mutex);

	unlock_on_fail = true;

	// check whether the ring is empty;
	// we may have been woken up because our peer closed their end, so we might not have any messages to receive
	if (simple_ring_queued_count(&private_channel->messages) == 0) {
		status = ferr_permanent_outage;
		goto out;
	}

out:
	if (status != ferr_ok) {
		if (unlock_on_fail) {
			flock_mutex_unlock(&private_channel->mutex);
		}

		if (increment_on_fail) {
			flock_semaphore_up(&private_channel->message_removal_semaphore);
		}
	}

	out_lock_state->flags = flags;
	out_lock_state->queue_emptied = false;
	out_lock_state->dequeued = false;

	return status;
};

void fchannel_unlock_receive(fchannel_t* channel, fchannel_receive_lock_state_t* in_lock_state) {
	fchannel_private_t* private_channel = (void*)channel;

	if (!in_lock_state->dequeued) {
		// if we didn't actually dequeue any messages, increment the removal semaphore back up.
		flock_semaphore_up(&private_channel->message_removal_semaphore);
	} else {
		// otherwise, we did dequeue a message, so there's a bit more logic to perform

		// there's now another slot available; increment the insertion semaphore
		flock_semaphore_up(&private_channel->message_insertion_semaphore);

		// now wake up the queue removal waitq
		fwaitq_wake_many(&private_channel->base.queue_removal_waitq, SIZE_MAX);

		// if we emptied our message queue, notify anyone that wants to know by waking up that waitq
		if (in_lock_state->queue_emptied) {
			fwaitq_wake_many(&private_channel->base.queue_empty_waitq, SIZE_MAX);
		}
	}

	flock_mutex_unlock(&private_channel->mutex);
};

void fchannel_receive_locked(fchannel_t* channel, bool peek, fchannel_message_t* out_message, fchannel_receive_lock_state_t* in_out_lock_state) {
	fchannel_private_t* private_channel = (void*)channel;

	// you can peek as many times as you want after locking,
	// but you can only dequeue a message once while locked,
	// and you cannot peek anymore after dequeuing a message.
	if (in_out_lock_state->dequeued) {
		fpanic("Invalid locked receive state");
	}

	// now let's remove/peek our message
	if ((peek ? simple_ring_peek : simple_ring_dequeue)(&private_channel->messages, out_message, 1) != 1) {
		// if we've successfully decremented the removal semaphore,
		// acquired the mutex, and seen that the channel has messages,
		// it is IMPOSSIBLE for dequeuing/peeking the message to fail.
		fpanic("Invalid message queue state");
	}

	if (!peek) {
		in_out_lock_state->dequeued = true;

		// the queue may now be empty, in which case we need to wake up the waitq for our peer
		in_out_lock_state->queue_emptied = simple_ring_queued_count(&private_channel->messages) == 0;
	}
};

ferr_t fchannel_receive(fchannel_t* channel, fchannel_receive_flags_t flags, fchannel_message_t* out_message) {
	fchannel_private_t* private_channel = (void*)channel;
	ferr_t status = ferr_ok;
	bool increment_semaphore_on_fail = false;
	bool queue_emptied = false;

	if ((flags & fchannel_receive_flag_no_wait) != 0) {
		if (flock_semaphore_try_down(&private_channel->message_removal_semaphore) != ferr_ok) {
			status = ferr_no_wait;
			goto out;
		}
	} else {
		if (flags & fchannel_receive_flag_interruptible) {
			status = flock_semaphore_down_interruptible(&private_channel->message_removal_semaphore);
			if (status != ferr_ok) {
				goto out;
			}
		} else {
			flock_semaphore_down(&private_channel->message_removal_semaphore);
		}
	}

	increment_semaphore_on_fail = true;

	// now let's acquire the lock to try to remove our message
	flock_mutex_lock(&private_channel->mutex);

	// check whether the ring is empty;
	// we may have been woken up because our peer closed their end, so we might not have any messages to receive
	if (simple_ring_queued_count(&private_channel->messages) == 0) {
		status = ferr_permanent_outage;
		flock_mutex_unlock(&private_channel->mutex);
		goto out;
	}

	// now let's remove our message
	if (simple_ring_dequeue(&private_channel->messages, out_message, 1) != 1) {
		// if we've successfully decremented the removal
		// semaphore, acquired the mutex, and seen that the channel has messages,
		// it is IMPOSSIBLE for dequeuing the message to fail.
		fpanic("Invalid message queue state");
	}

	// the queue may now be empty, in which case we need to wake up the waitq for our peer
	queue_emptied = simple_ring_queued_count(&private_channel->messages) == 0;

	// there's now another slot available; increment the insertion semaphore
	flock_semaphore_up(&private_channel->message_insertion_semaphore);

	// now wake up the queue removal waitq
	fwaitq_wake_many(&private_channel->base.queue_removal_waitq, SIZE_MAX);

	// if we emptied our message queue, notify anyone that wants to know by waking up that waitq
	if (queue_emptied) {
		fwaitq_wake_many(&private_channel->base.queue_empty_waitq, SIZE_MAX);
	}

	flock_mutex_unlock(&private_channel->mutex);

out:
	if (status != ferr_ok) {
		if (increment_semaphore_on_fail) {
			flock_semaphore_up(&private_channel->message_removal_semaphore);
		}
	}

	return status;
};

ferr_t fchannel_close(fchannel_t* channel) {
	fchannel_private_t* private_channel = (void*)channel;
	ferr_t status = ferr_ok;

	flock_mutex_lock(&private_channel->peer->mutex);

	if ((private_channel->peer->flags & fchannel_flag_closed_receive) != 0) {
		status = ferr_already_in_progress;
	} else {
		private_channel->peer->flags |= fchannel_flag_closed_receive;
	}

	flock_mutex_unlock(&private_channel->peer->mutex);

	if (status == ferr_already_in_progress) {
		flock_mutex_lock(&private_channel->mutex);

		if ((private_channel->flags & fchannel_flag_closed_receive) != 0) {
			status = ferr_permanent_outage;
		}

		flock_mutex_unlock(&private_channel->mutex);
	}

	if (status == ferr_ok) {
		// increment our peer's removal semaphore so anyone waiting to receive a message on our peer from us wakes up
		flock_semaphore_up(&private_channel->peer->message_removal_semaphore);
		// increment our peer's insertion semaphore so anyone waiting to send a message from us to our peer wakes up
		flock_semaphore_up(&private_channel->peer->message_insertion_semaphore);
		// wake up anyone waiting for us to close
		fwaitq_wake_many(&private_channel->base.close_waitq, SIZE_MAX);
	}

	return status;
};

void fchannel_message_destroy(fchannel_message_t* message) {
	if (message->attachments) {
		for (const fchannel_message_attachment_header_t* header = &message->attachments[0]; header != NULL; header = (header->next_offset == 0) ? NULL : (const void*)((const char*)header + header->next_offset)) {
			switch (header->type) {
				case fchannel_message_attachment_type_channel: {
					const fchannel_message_attachment_channel_t* channel_attachment = (const void*)header;

					if (channel_attachment->channel) {
						fchannel_release(channel_attachment->channel);
					}
				} break;

				case fchannel_message_attachment_type_mapping: {
					const fchannel_message_attachment_mapping_t* mapping_attachment = (const void*)header;

					if (mapping_attachment->mapping) {
						fpage_mapping_release(mapping_attachment->mapping);
					}
				} break;

				case fchannel_message_attachment_type_data: {
					const fchannel_message_attachment_data_t* data_attachment = (const void*)header;

					if (data_attachment->flags & fchannel_message_attachment_data_flag_shared) {
						if (data_attachment->shared_data) {
							fpage_mapping_release(data_attachment->shared_data);
						}
					} else {
						if (data_attachment->copied_data) {
							fpanic_status(fmempool_free(data_attachment->copied_data));
						}
					}
				} break;

				case fchannel_message_attachment_type_null:
				default:
					// no special processing for this attachment type
					break;
			}
		}

		fpanic_status(fmempool_free(message->attachments));
	}

	if (message->body) {
		fpanic_status(fmempool_free(message->body));
	}
};

fchannel_t* fchannel_peer(fchannel_t* channel, bool retain) {
	fchannel_private_t* private_channel = (void*)channel;
	if (retain) {
		if (fchannel_retain((void*)private_channel->peer) == ferr_ok) {
			return (void*)private_channel->peer;
		} else {
			return NULL;
		}
	} else {
		return (void*)private_channel->peer;
	}
};

fchannel_message_id_t fchannel_next_message_id(fchannel_t* channel) {
	fchannel_private_t* channel1 = fchannel_get_1((void*)channel);
	fchannel_message_id_t message_id = fchannel_message_id_invalid;

retry:
	message_id = __atomic_fetch_add(&channel1->channel_1.next_message_id, 1, __ATOMIC_RELAXED);

	if (message_id == fchannel_message_id_invalid) {
		goto retry;
	}

	return message_id;
};
