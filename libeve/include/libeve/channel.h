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

#ifndef _LIBEVE_CHANNEL_H_
#define _LIBEVE_CHANNEL_H_

#include <libeve/base.h>
#include <libeve/objects.h>
#include <libeve/item.h>
#include <libeve/loop.h>

LIBEVE_DECLARATIONS_BEGIN;

LIBEVE_ITEM_CLASS(channel);

LIBEVE_ENUM(uint64_t, eve_channel_cancellation_token) {
	eve_channel_cancellation_token_invalid = 0,
};

typedef void (*eve_channel_message_handler_f)(void* context, eve_channel_t* channel, sys_channel_message_t* message);
typedef void (*eve_channel_peer_close_handler_f)(void* context, eve_channel_t* channel);
typedef void (*eve_channel_message_send_error_handler_f)(void* context, eve_channel_t* channel, sys_channel_message_t* message, ferr_t error);

/**
 * Invoked whenever a reply is received or an error occurs while trying to send the original message.
 *
 * @param context The context provided to eve_channel_send_with_reply_async().
 * @param channel The channel that the message was sent/received on.
 * @param message Either the received reply (if @p status is ::ferr_ok) or the failed outgoing message (if non-null).
 *                If this is null, then the outgoing message has been consumed elsewhere (probably passed on to the
 *                default message send error handler for the channel).
 * @param status  If ::ferr_ok, the message was sent and a reply has been received successfully.
 *                Otherwise, the message has not been sent and this parameter provides some info as to why.
 *                @see eve_channel_send_with_reply_sync for possible status codes.
 *
 * In some cases, the reply handler has to be invoked before the message has a chance to be sent. For example,
 * when the peer closes their end, the reply handler is notified once the incoming message queue has been emptied and
 * no reply has been found. In this case, the reply handler is invoked with a null message and status of ::ferr_permanent_outage.
 * There will still be an attempt to send the message, however. If this fails, then the channel's send message error handler is
 * invoked instead. This ensures that 1) the reply handler is never invoked more than once, and 2) the outgoing message is not
 * dropped without the caller's knowledge.
 *
 * TODO: we should instead ensure that the reply handler is never invoked until after an attempt is made to send the message.
 */
typedef void (*eve_channel_reply_handler_f)(void* context, eve_channel_t* channel, sys_channel_message_t* message, ferr_t status);

LIBEVE_WUR ferr_t eve_channel_create(sys_channel_t* sys_channel, void* context, eve_channel_t** out_channel);
void eve_channel_set_message_handler(eve_channel_t* channel, eve_channel_message_handler_f handler);
void eve_channel_set_peer_close_handler(eve_channel_t* channel, eve_channel_peer_close_handler_f handler);
void eve_channel_set_message_send_error_handler(eve_channel_t* channel, eve_channel_message_send_error_handler_f handler);

/**
 * Sends the given message.
 *
 * If @p synchronous is true, this function blocks waiting for the message to be sent.
 * In this case, message send errors are reported back to the caller via this function's return code.
 *
 * Additionally, this function is suspendable when @p synchronous is true: if called inside a loop work item,
 * it will suspend the work item and automatically resume it when the message has been sent.
 *
 * If @p synchronous is false, this function does not block; it only enqueues the message to be sent.
 * In this case, message send errors are handled by the channel's message send error handler.
 */
LIBEVE_WUR ferr_t eve_channel_send(eve_channel_t* channel, sys_channel_message_t* message, bool synchronous);
LIBEVE_WUR ferr_t eve_channel_target(eve_channel_t* channel, bool retain, sys_channel_t** out_sys_channel);

LIBEVE_WUR ferr_t eve_channel_conversation_create(eve_channel_t* channel, sys_channel_conversation_id_t* out_conversation_id);

/**
 * Sends the given message and waits for a reply asynchronously.
 * When the reply is received, the given reply handler is scheduled to run on the loop that the reply was received on.
 *
 * This function does **not** block waiting for the message to send nor for the reply to arrive.
 * It queues the message to be sent and returns immediately.
 *
 * @param channel       The channel to send the message and receive the reply on.
 * @param message       The message to send. This message must have a valid conversation ID (i.e. not equal to ::sys_channel_conversation_id_none).
 * @param reply_handler The reply handler to invoke when a reply is received. This handler is also invoked if an error occurs after the message has
 *                      already been queued. @see eve_channel_reply_handler_f for more details.
 * @param context       An optional context to pass to the reply handler when it is invoked.
 *
 * @retval ferr_ok               The message has been successfully queued to be sent.
 * @retval ferr_temporary_outage There were not enough resources to queue the message to be sent.
 * @retval ferr_invalid_argument The message had an invalid conversation ID (::sys_channel_conversation_id_none).
 */
LIBEVE_WUR ferr_t eve_channel_send_with_reply_async(eve_channel_t* channel, sys_channel_message_t* message, eve_channel_reply_handler_f reply_handler, void* context);

/**
 * Sends the given message and waits for a reply synchronously.
 *
 * This function **does** block waiting for both the message to send and the reply to arrive.
 * However, this function is suspendable: if called inside a loop work item, it will suspend
 * the work item and automatically resume it when the reply is received.
 *
 * @param      channel   The channel to send the message and receive the reply on.
 * @param      message   The message to send. This message must have a valid conversation ID (i.e. not equal to ::sys_channel_conversation_id_none).
 * @param[out] out_reply A pointer in which to write a reference to the received reply message on success.
 *
 * @retval ferr_ok               The message was successfully sent and a reply has been received.
 * @retval ferr_temporary_outage There were not enough resources to send the message.
 * @retval ferr_invalid_argument The message had an invalid conversation ID (::sys_channel_conversation_id_none).
 * @retval ferr_permanent_outage The peer closed their end before a reply could be received.
 *
 * TODO: there may be more status codes that could be returned.
 */
LIBEVE_WUR ferr_t eve_channel_send_with_reply_sync(eve_channel_t* channel, sys_channel_message_t* message, sys_channel_message_t** out_reply);

LIBEVE_WUR ferr_t eve_channel_receive_conversation_async(eve_channel_t* channel, sys_channel_conversation_id_t conversation_id, eve_channel_reply_handler_f reply_handler, void* context, eve_channel_cancellation_token_t* out_cancellation_token);
LIBEVE_WUR ferr_t eve_channel_receive_conversation_cancel(eve_channel_t* channel, sys_channel_conversation_id_t conversation_id, eve_channel_cancellation_token_t cancellation_token);

LIBEVE_WUR ferr_t eve_channel_receive_conversation_sync(eve_channel_t* channel, sys_channel_conversation_id_t conversation_id, sys_channel_message_t** out_reply);

LIBEVE_DECLARATIONS_END;

#endif // _LIBEVE_CHANNEL_H_
