/*
 * This file is part of Anillo OS
 * Copyright (C) 2023 Anillo OS Developers
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

#ifndef _LIBSYS_SYSMAN_PRIVATE_H_
#define _LIBSYS_SYSMAN_PRIVATE_H_

#include <libsys/sysman.h>
#include <libeve/channel.h>

LIBSYS_DECLARATIONS_BEGIN;

LIBSYS_ENUM(uint8_t, sys_sysman_rpc_function) {
	sys_sysman_rpc_function_invalid = 0,
	sys_sysman_rpc_function_connect = 1,
	sys_sysman_rpc_function_register = 2,
	sys_sysman_rpc_function_subchannel = 3,
};

LIBSYS_STRUCT(sys_sysman_rpc_call_header) {
	sys_sysman_rpc_function_t function;
};

LIBSYS_STRUCT(sys_sysman_rpc_reply_header) {
	sys_sysman_rpc_function_t function;
	ferr_t status;
};

LIBSYS_STRUCT(sys_sysman_rpc_call_connect) {
	sys_sysman_rpc_call_header_t header;
	char name[];
};

LIBSYS_STRUCT(sys_sysman_rpc_reply_connect) {
	sys_sysman_rpc_reply_header_t header;
};

LIBSYS_STRUCT(sys_sysman_rpc_call_register) {
	sys_sysman_rpc_call_header_t header;
	sys_sysman_realm_t realm;
	char name[];
};

LIBSYS_STRUCT(sys_sysman_rpc_reply_register) {
	sys_sysman_rpc_reply_header_t header;
};

LIBSYS_STRUCT(sys_sysman_rpc_call_subchannel) {
	sys_sysman_rpc_call_header_t header;
};

LIBSYS_STRUCT(sys_sysman_rpc_reply_subchannel) {
	sys_sysman_rpc_reply_header_t header;
};

LIBSYS_UNION(sys_sysman_rpc_call) {
	sys_sysman_rpc_call_header_t header;
	sys_sysman_rpc_call_connect_t connect;
	sys_sysman_rpc_call_register_t register_;
	sys_sysman_rpc_call_subchannel_t subchannel;
};

LIBSYS_UNION(sys_sysman_rpc_reply) {
	sys_sysman_rpc_reply_header_t header;
	sys_sysman_rpc_reply_connect_t connect;
	sys_sysman_rpc_reply_register_t register_;
	sys_sysman_rpc_reply_subchannel_t subchannel;
};

extern eve_channel_t* __sys_sysman_eve_channel;

LIBSYS_WUR ferr_t sys_sysman_init(void);

LIBSYS_WUR ferr_t sys_sysman_create_subchannel(sys_channel_t** out_subchannel);

LIBSYS_DECLARATIONS_END;

#endif // _LIBSYS_SYSMAN_PRIVATE_H_
