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

#ifndef _FERRO_API_H_
#define _FERRO_API_H_

#include <ferro/base.h>

#include <stdint.h>

FERRO_DECLARATIONS_BEGIN;

FERRO_ENUM(uint64_t, fchannel_server_accept_flags) {
	fserver_channel_accept_flag_no_wait = 1 << 0,
};

FERRO_ENUM(uint64_t, fchannel_send_flags) {
	fchannel_send_flag_no_wait            = 1 << 0,
	fchannel_send_flag_start_conversation = 1 << 1,
};

FERRO_ENUM(uint64_t, fchannel_conversation_id) {
	fchannel_conversation_id_none = 0,
};

FERRO_ENUM(uint8_t, fchannel_message_attachment_type) {
	fchannel_message_attachment_type_invalid = 0,
	fchannel_message_attachment_type_null    = 1,
	fchannel_message_attachment_type_channel = 2,
	fchannel_message_attachment_type_mapping = 3,
};

FERRO_ENUM(uint64_t, fchannel_message_id) {
	fchannel_message_id_invalid = UINT64_MAX,
};

FERRO_DECLARATIONS_END;

#endif // _FERRO_API_H_
