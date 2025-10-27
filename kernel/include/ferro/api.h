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
#include <ferro/platform.h>

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
	fchannel_message_attachment_type_invalid        = 0,
	fchannel_message_attachment_type_null           = 1,
	fchannel_message_attachment_type_channel        = 2,
	fchannel_message_attachment_type_mapping        = 3,
	fchannel_message_attachment_type_data           = 4,
};

FERRO_ENUM(uint64_t, fchannel_message_id) {
	fchannel_message_id_invalid = UINT64_MAX,
};

FERRO_ENUM(uint64_t, fchannel_peer_id) {
	fchannel_peer_id_invalid = UINT64_MAX,
	fchannel_peer_id_kernel = UINT64_MAX - 1,
	fchannel_peer_id_unknown_userspace = UINT64_MAX - 2,
};

#if FERRO_ARCH == FERRO_ARCH_x86_64
FERRO_STRUCT(ferro_thread_context) {
	uint64_t rax;
	uint64_t rcx;
	uint64_t rdx;
	uint64_t rbx;
	uint64_t rsi;
	uint64_t rdi;
	uint64_t rsp;
	uint64_t rbp;
	uint64_t r8;
	uint64_t r9;
	uint64_t r10;
	uint64_t r11;
	uint64_t r12;
	uint64_t r13;
	uint64_t r14;
	uint64_t r15;
	uint64_t rip;
	uint64_t rflags;
	void* xsave_area;
	uint64_t xsave_area_size;
};
#elif FERRO_ARCH == FERRO_ARCH_aarch64
FERRO_STRUCT(ferro_thread_context) {
	uint64_t x0;
	uint64_t x1;
	uint64_t x2;
	uint64_t x3;
	uint64_t x4;
	uint64_t x5;
	uint64_t x6;
	uint64_t x7;
	uint64_t x8;
	uint64_t x9;
	uint64_t x10;
	uint64_t x11;
	uint64_t x12;
	uint64_t x13;
	uint64_t x14;
	uint64_t x15;
	uint64_t x16;
	uint64_t x17;
	uint64_t x18;
	uint64_t x19;
	uint64_t x20;
	uint64_t x21;
	uint64_t x22;
	uint64_t x23;
	uint64_t x24;
	uint64_t x25;
	uint64_t x26;
	uint64_t x27;
	uint64_t x28;
	uint64_t x29;
	uint64_t x30;
	uint64_t pc;
	uint64_t sp;
	uint64_t pstate;
	uint64_t fpsr;
	uint64_t fpcr;
	__uint128_t* fp_registers;
};
#endif

FERRO_STRUCT(ferro_constants) {
	uint64_t page_size;
	uint64_t minimum_stack_size;

	// including padding
	uint64_t total_thread_context_size;

	uint64_t minimum_thread_context_alignment_power;

#if FERRO_ARCH == FERRO_ARCH_x86_64
	uint64_t xsave_area_size;
#endif
};

FERRO_DECLARATIONS_END;

#endif // _FERRO_API_H_
