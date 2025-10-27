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

#ifndef _FERRO_CORE_CHANNELS_PRIVATE_H_
#define _FERRO_CORE_CHANNELS_PRIVATE_H_

#include <ferro/core/channels.h>
#include <libsimple/libsimple.h>
#include <ferro/core/refcount.h>

FERRO_DECLARATIONS_BEGIN;

FERRO_ENUM(uint64_t, fchannel_flags) {
	/**
	 * If one end of a channel is closed, it can no longer send messages.
	 * A channel is only fully closed once both ends are closed.
	 *
	 * However, the closure flag is set on the peer end of the channel,
	 * because the peers mutex is the one that needs to be held in order to try to send a message.
	 */
	fchannel_flag_closed_receive = 1 << 0,
};

FERRO_STRUCT_FWD(fchannel_conversation_private);

FERRO_STRUCT(fchannel_private) {
	fchannel_t base;

	bool is_channel_0;

	// no need to worry about our peer being freed (thus no need to zero this or lock it in any way);
	// channels come in pairs are only freed once both are fully released.
	fchannel_private_t* peer;

	/**
	 * Protects #flags and #messages.
	 */
	flock_mutex_t mutex;

	/**
	 * This is protected by #mutex, but as an optimization, semaphore waiters can check whether ::fchannel_flag_closed
	 * has been set before trying to take #mutex after they're awoken. This allows them to avoid needlessly acquiring the lock.
	 */
	fchannel_flags_t flags;

	simple_ring_t messages;
	flock_semaphore_t message_insertion_semaphore;
	flock_semaphore_t message_removal_semaphore;

	frefcount_t closure_refcount;

	// DO NOT ACCESS WITHOUT CHECKING WHICH CHANNEL IN THE PAIR THIS IS.
	// each channel in a pair contains half of the data shared between both channels,
	// to reduce the overall size of the pair.
	union {
		struct {
			frefcount_t destruction_refcount;
		} channel_0;
		struct {
			uint64_t next_conversation_id;
			fchannel_message_id_t next_message_id;
		} channel_1;
	};
};

FERRO_ALWAYS_INLINE
fchannel_private_t* fchannel_get_0(fchannel_private_t* private_channel) {
	if (private_channel->is_channel_0) {
		return private_channel;
	} else {
		return private_channel->peer;
	}
};

FERRO_ALWAYS_INLINE
fchannel_private_t* fchannel_get_1(fchannel_private_t* private_channel) {
	if (private_channel->is_channel_0) {
		return private_channel->peer;
	} else {
		return private_channel;
	}
};

FERRO_STRUCT(fchannel_receive_lock_state) {
	fchannel_receive_flags_t flags;
	bool queue_emptied;
	bool dequeued;
};

fchannel_message_id_t fchannel_next_message_id(fchannel_t* channel);

FERRO_WUR ferr_t fchannel_lock_receive(fchannel_t* channel, fchannel_receive_flags_t flags, fchannel_receive_lock_state_t* out_lock_state);
void fchannel_unlock_receive(fchannel_t* channel, fchannel_receive_lock_state_t* in_lock_state);
void fchannel_receive_locked(fchannel_t* channel, bool peek, fchannel_message_t* out_message, fchannel_receive_lock_state_t* in_out_lock_state);

FERRO_STRUCT(fchannel_send_lock_state) {
	fchannel_send_flags_t flags;
	bool enqueued;
	bool queue_filled;
};

FERRO_WUR ferr_t fchannel_lock_send(fchannel_t* channel, fchannel_send_flags_t flags, fchannel_send_lock_state_t* out_lock_state);
void fchannel_unlock_send(fchannel_t* channel, fchannel_send_lock_state_t* in_lock_state);
void fchannel_send_locked(fchannel_t* channel, fchannel_message_t* in_out_message, fchannel_send_lock_state_t* in_out_lock_state);

FERRO_DECLARATIONS_END;

#endif // _FERRO_CORE_CHANNELS_PRIVATE_H_
